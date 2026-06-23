/**
    bespoke synth, a software modular synthesizer
    Copyright (C) 2021 Ryan Challinor (contact: awwbees@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**/

#if BESPOKE_WINDOWS
#define ssize_t ssize_t_undef_hack
#endif
#include "SardineModule.h"
#include "PatchCableSource.h"
#include "ScriptModule.h"
#include "SynthGlobals.h"
#include "Transport.h"
#include "UIControlMacros.h"
#if BESPOKE_WINDOWS
#undef ssize_t
#endif

#include "leathers/push"
#include "leathers/unused-value"
#include "leathers/range-loop-analysis"
#include "pybind11/embed.h"
#include "pybind11/stl.h"
#include "leathers/pop"

namespace py = pybind11;
using namespace pybind11::literals;
using namespace juce;

namespace
{
   const char* kSardineScriptFolder = "sardine";
}

std::vector<SardineModule*> SardineModule::sSardineModules;

PYBIND11_EMBEDDED_MODULE(sardinebespoke, m)
{
   m.def("get_me", [](int moduleIndex)
   {
      return SardineModule::sSardineModules[moduleIndex];
   }, py::return_value_policy::reference);

   py::class_<SardineModule>(m, "sardine_module")
      .def("send_note", &SardineModule::SendNote, "delay"_a, "pitch"_a, "velocity"_a = 100, "length"_a = 1.0 / 16.0, "pan"_a = .5, "channel"_a = -1)
      .def("send_cc", &SardineModule::SendControl, "delay"_a, "control"_a, "value"_a, "channel"_a = -1)
      .def("schedule", &SardineModule::ScheduleCode, "delay"_a, "code"_a)
      .def("stop", &SardineModule::Stop)
      .def("panic", &SardineModule::Stop)
      .def("measure", &SardineModule::GetCurrentMeasureTime)
      .def("time", &SardineModule::GetCurrentRunTime)
      .def("print", &SardineModule::PrintText);
}

namespace
{
   const char* kDefaultSardineCode =
      "# Bespoke Sardine\n"
      "# N(\"60 64 67 72\", div=8, dur=1/16)\n"
      "#\n"
      "# def d():\n"
      "#     N(\"60 . 67 72\", div=8, dur=1/16)\n"
      "#     again(1, d)\n"
      "# d()\n";
}

SardineModule::SardineModule()
: IDrawableModule(520, 365)
{
   ScriptModule::CheckIfPythonEverSuccessfullyInitialized();
   mModuleIndex = sSardineModules.size();
   sSardineModules.push_back(this);
   ResetScheduledEvents();
}

SardineModule::~SardineModule()
{
   if (mModuleIndex < sSardineModules.size())
      sSardineModules[mModuleIndex] = nullptr;
}

void SardineModule::CreateUIControls()
{
   IDrawableModule::CreateUIControls();

   UIBLOCK0();
   DROPDOWN(mLoadScriptSelector, "loadscript", &mLoadScriptIndex, 120);
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mLoadScriptButton, "load");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mSaveScriptButton, "save as");
   UIBLOCK_NEWLINE();
   UICONTROL_CUSTOM(mCodeEntry, new CodeEntry(UICONTROL_BASICS("code"), 500, 300));
   BUTTON(mRunButton, "run");
   UIBLOCK_SHIFTRIGHT();
   BUTTON(mStopButton, "stop");
   UIBLOCK_SHIFTRIGHT();
   CHECKBOX(mSyncCheckbox, "sync", &mSyncToTransport);
   UIBLOCK_NEWLINE();
   FLOATSLIDER(mASlider, "a", &mA, 0, 1);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mBSlider, "b", &mB, 0, 1);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mCSlider, "c", &mC, 0, 1);
   UIBLOCK_SHIFTRIGHT();
   FLOATSLIDER(mDSlider, "d", &mD, 0, 1);
   ENDUIBLOCK(mWidth, mHeight);

   mCodeEntry->SetDoSyntaxHighlighting(true);
   mCodeEntry->SetText(kDefaultSardineCode);
   RefreshScriptFiles();
}

void SardineModule::Poll()
{
   if (!mEnabled)
      return;

   ScriptModule::InitializePythonIfNecessary();
   InstallPythonHelpers();
   ProcessScheduledEvents(gTime + TheTransport->GetEventLookaheadMs());
}

void SardineModule::PostRepatch(PatchCableSource* cableSource, bool fromUserClick)
{
   if (cableSource->GetTarget() != nullptr && cableSource->GetConnectionType() == kConnectionType_Note)
      Transport::sDoEventLookahead = true;
}

void SardineModule::ButtonClicked(ClickButton* button, double time)
{
   if (button == mRunButton)
   {
      Stop();
      if (mSyncToTransport)
         RunEditorSynced(time);
      else
         RunEditor(time);
   }
   if (button == mStopButton)
      Stop();
   if (button == mLoadScriptButton)
      LoadSelectedScript();
   if (button == mSaveScriptButton)
      SaveScriptAs();
}

void SardineModule::DropdownClicked(DropdownList* list)
{
   if (list == mLoadScriptSelector)
      RefreshScriptFiles();
}

void SardineModule::ExecuteCode()
{
   Stop();
   if (mSyncToTransport)
      RunEditorSynced(NextBufferTime(false));
   else
      RunEditor(NextBufferTime(false));
}

std::pair<int, int> SardineModule::ExecuteBlock(int lineStart, int lineEnd)
{
   Stop();
   if (mSyncToTransport)
      RunEditorSynced(NextBufferTime(false));
   else
      RunEditor(NextBufferTime(false));
   return std::make_pair(lineStart, lineEnd);
}

void SardineModule::RunEditor(double time)
{
   mCodeEntry->Publish();
   RunCode(time, mCodeEntry->GetText(true));
}

void SardineModule::RunEditorSynced(double time)
{
   mCodeEntry->Publish();
   double startTime = GetNextBarStartTime(time);
   ScheduleCodeAtTime(startTime, mCodeEntry->GetText(true));
   mCodeEntry->SetError(false);
   mLastError = "";
   mLastPrint = "queued beat 1";
   mLastPrintTime = gTime;
}

void SardineModule::RunCode(double time, const std::string& code)
{
   if (!ScriptModule::sPythonInitialized)
   {
      TheSynth->LogEvent("trying to run sardine code before python is initialized", kLogEventType_Error);
      return;
   }

   mCurrentRunTime = time;
   ComputeSliders(0);
   InstallPythonHelpers();

   try
   {
      py::exec("sardine = sardinebespoke.get_me(" + ofToString(mModuleIndex) + ")", py::globals());
      py::exec("a = " + ofToString(mA) + "; b = " + ofToString(mB) + "; c = " + ofToString(mC) + "; d = " + ofToString(mD), py::globals());
      py::exec(code, py::globals());
      mCodeEntry->SetError(false);
      mLastError = "";
      mLastPrint = "ran";
      mLastPrintTime = gTime;
   }
   catch (pybind11::error_already_set& e)
   {
      mLastError = (std::string)py::str(e.type()) + ": " + (std::string)py::str(e.value());
      mCodeEntry->SetError(true, 0);
      TheSynth->LogEvent("sardine python exception: " + mLastError, kLogEventType_Error);
   }
}

void SardineModule::InstallPythonHelpers()
{
   if (mPythonHelpersInstalled)
      return;

   py::exec(R"PY(
import math
import random
import sardinebespoke

_NOTE_NAMES = {"c": 0, "cs": 1, "db": 1, "d": 2, "ds": 3, "eb": 3, "e": 4, "f": 5, "fs": 6, "gb": 6, "g": 7, "gs": 8, "ab": 8, "a": 9, "as": 10, "bb": 10, "b": 11}
_AGAIN_CALLBACKS = {}
_AGAIN_NEXT_ID = 0

def _coerce_note(value):
    if value is None:
        return None
    if isinstance(value, (int, float)):
        return value
    token = str(value).strip().lower().replace("#", "s")
    if token in (".", "_", "~", "r", "rest", ""):
        return None
    try:
        return float(token)
    except ValueError:
        pass
    name = token[:-1]
    octave = token[-1:]
    if name in _NOTE_NAMES and octave.lstrip("-").isdigit():
        return 12 * (int(octave) + 1) + _NOTE_NAMES[name]
    if token in _NOTE_NAMES:
        return 60 + _NOTE_NAMES[token]
    raise ValueError("unknown note: " + str(value))

def _tokens(pattern):
    if isinstance(pattern, str):
        for char in "[],{}":
            pattern = pattern.replace(char, " ")
        return pattern.split()
    try:
        return list(pattern)
    except TypeError:
        return [pattern]

def _coerce_channel(channel):
    if channel is None:
        return -1
    channel = int(channel)
    if channel < 1 or channel > 16:
        raise ValueError("channel must be 1..16")
    return channel

def N(note=60, vel=100, dur=1/16, div=None, pan=.5, delay=0, channel=None, ch=None):
    if ch is not None:
        channel = ch
    channel = _coerce_channel(channel)
    notes = _tokens(note)
    if not notes:
        return
    step = 0 if div in (None, 0) else 1 / float(div)
    for index, item in enumerate(notes):
        pitch = _coerce_note(item)
        if pitch is not None:
            sardine.send_note(float(delay) + index * step, float(pitch), float(vel), float(dur), float(pan), channel)

def CC(control, value, delay=0, channel=None, ch=None):
    if ch is not None:
        channel = ch
    sardine.send_cc(float(delay), int(control), int(value), _coerce_channel(channel))

def _run_again_callback(callback_id):
    callback = _AGAIN_CALLBACKS.pop(int(callback_id), None)
    if callback is None:
        return
    globals()[callback.__name__] = callback
    callback()

def again(delay, callback):
    if callable(callback):
        global _AGAIN_NEXT_ID
        callback_id = _AGAIN_NEXT_ID
        _AGAIN_NEXT_ID += 1
        _AGAIN_CALLBACKS[callback_id] = callback
        sardine.schedule(float(delay), "_run_again_callback(" + str(callback_id) + ")")
    else:
        sardine.schedule(float(delay), str(callback))

def panic():
    sardine.panic()

def swim(callback, every=1):
    callback()
    again(every, "swim(" + callback.__name__ + ", " + repr(every) + ")")

def now():
    return sardine.measure()
)PY",
            py::globals());

   mPythonHelpersInstalled = true;
}

void SardineModule::SendNote(double delayMeasureTime, float pitch, float velocity, float lengthMeasureTime, float pan, int channel)
{
   Transport::sDoEventLookahead = true;

   double noteOnTime = mCurrentRunTime + delayMeasureTime * TheTransport->MsPerBar();
   PlayNoteNow(noteOnTime, pitch, velocity, pan, channel, noteOnTime > gTime);

   double noteOffTime = noteOnTime + lengthMeasureTime * TheTransport->MsPerBar();
   PlayNoteNow(noteOffTime, pitch, 0, pan, channel, true);

   mLastPrint = "note " + ofToString(int(pitch + .5f)) + " vel " + ofToString(int(velocity));
   if (channel != -1)
      mLastPrint += " ch " + ofToString(channel);
   mLastPrintTime = gTime;
}

void SardineModule::SendControl(double delayMeasureTime, int control, int value, int channel)
{
   double time = mCurrentRunTime + delayMeasureTime * TheTransport->MsPerBar();
   int voiceIdx = channel == -1 ? -1 : channel - 1;
   if (time <= gTime)
      SendCCOutput(control, value, voiceIdx);
   else
      ScheduleCode(delayMeasureTime, "CC(" + ofToString(control) + "," + ofToString(value) + ", channel=" + ofToString(channel) + ")");
}

void SardineModule::ScheduleCode(double delayMeasureTime, std::string code)
{
   ScheduleCodeAtTime(mCurrentRunTime + delayMeasureTime * TheTransport->MsPerBar(), std::move(code));
}

void SardineModule::ScheduleCodeAtTime(double scheduledTime, std::string code)
{
   for (auto& event : mScheduledCode)
   {
      if (event.time == -1)
      {
         event.time = scheduledTime;
         event.code = std::move(code);
         return;
      }
   }

   mLastError = "too many scheduled callbacks";
   mCodeEntry->SetError(true, 0);
   TheSynth->LogEvent("sardine schedule queue is full", kLogEventType_Error);
}

double SardineModule::GetNextBarStartTime(double time) const
{
   double referenceTime = MAX(time, gTime + TheTransport->GetEventLookaheadMs());
   double measureTime = TheTransport->GetMeasureTime(referenceTime);
   double nextMeasure = floor(measureTime) + 1;
   return referenceTime + (nextMeasure - measureTime) * TheTransport->MsPerBar();
}

void SardineModule::Stop()
{
   mNoteOutput.Flush(NextBufferTime(false));
   ResetScheduledEvents();
}

double SardineModule::GetCurrentMeasureTime() const
{
   return TheTransport->GetMeasureTime(mCurrentRunTime);
}

void SardineModule::PrintText(std::string text)
{
   mLastPrint = std::move(text);
   mLastPrintTime = gTime;
}

void SardineModule::PlayNoteNow(double time, float pitch, float velocity, float pan, int channel, bool exactTiming)
{
   int intPitch = int(pitch + .5f);
   int voiceIdx = channel == -1 ? -1 : channel - 1;
   ModulationParameters modulation;
   modulation.pan = pan;
   PlayNoteOutput(NoteMessage(time, intPitch, int(velocity), voiceIdx, modulation), exactTiming);
}

void SardineModule::ProcessScheduledEvents(double time)
{
   for (auto& note : mScheduledNotes)
   {
      if (note.time != -1 && note.time <= time)
      {
         PlayNoteNow(note.time, note.pitch, note.velocity, note.pan, -1, true);
         note.time = -1;
      }
   }

   for (auto& event : mScheduledCode)
   {
      if (event.time != -1 && event.time <= time)
      {
         std::string code = event.code;
         double eventTime = event.time;
         event.time = -1;
         RunCode(eventTime, code);
      }
   }
}

void SardineModule::ResetScheduledEvents()
{
   for (auto& note : mScheduledNotes)
      note.time = -1;
   for (auto& event : mScheduledCode)
      event.time = -1;
}

void SardineModule::DrawModule()
{
   if (Minimized() || IsVisible() == false)
      return;

   mLoadScriptSelector->Draw();
   mLoadScriptButton->Draw();
   mSaveScriptButton->Draw();
   mCodeEntry->Draw();
   mRunButton->Draw();
   mStopButton->Draw();
   mSyncCheckbox->Draw();
   mASlider->Draw();
   mBSlider->Draw();
   mCSlider->Draw();
   mDSlider->Draw();

   if (!mLastError.empty())
      DrawTextNormal(mLastError, 5, mHeight - 15);
   else if (mLastPrintTime > 0 && gTime - mLastPrintTime < 3000)
      DrawTextNormal(mLastPrint, 5, mHeight - 15);
}

void SardineModule::Resize(float w, float h)
{
   float entryW, entryH;
   mCodeEntry->GetDimensions(entryW, entryH);
   mCodeEntry->SetDimensions(entryW + w - mWidth, entryH + h - mHeight);
   mRunButton->SetPosition(mRunButton->GetPosition(true).x, mRunButton->GetPosition(true).y + h - mHeight);
   mStopButton->SetPosition(mStopButton->GetPosition(true).x, mStopButton->GetPosition(true).y + h - mHeight);
   mSyncCheckbox->SetPosition(mSyncCheckbox->GetPosition(true).x, mSyncCheckbox->GetPosition(true).y + h - mHeight);
   mASlider->SetPosition(mASlider->GetPosition(true).x, mASlider->GetPosition(true).y + h - mHeight);
   mBSlider->SetPosition(mBSlider->GetPosition(true).x, mBSlider->GetPosition(true).y + h - mHeight);
   mCSlider->SetPosition(mCSlider->GetPosition(true).x, mCSlider->GetPosition(true).y + h - mHeight);
   mDSlider->SetPosition(mDSlider->GetPosition(true).x, mDSlider->GetPosition(true).y + h - mHeight);
   mWidth = w;
   mHeight = h;
}

void SardineModule::RefreshScriptFiles()
{
   mScriptFilePaths.clear();
   mLoadScriptSelector->Clear();

   File scriptsDir(ofToDataPath(kSardineScriptFolder));
   scriptsDir.createDirectory();

   std::map<std::string, std::string> scripts;
   File bundledScriptsDir(ofToResourcePath(std::string("userdata_original/") + kSardineScriptFolder));
   for (const auto& entry : RangedDirectoryIterator{ bundledScriptsDir, false, "*.py" })
   {
      const auto& file = entry.getFile();
      scripts[file.getFileName().toStdString()] = file.getFullPathName().toStdString();
   }

   for (const auto& entry : RangedDirectoryIterator{ scriptsDir, false, "*.py" })
   {
      const auto& file = entry.getFile();
      scripts[file.getFileName().toStdString()] = file.getFullPathName().toStdString();
   }

   for (const auto& script : scripts)
   {
      mLoadScriptSelector->AddLabel(script.first, (int)mScriptFilePaths.size());
      mScriptFilePaths.push_back(script.second);
   }
}

void SardineModule::LoadSelectedScript()
{
   if (mLoadScriptIndex < 0 || mLoadScriptIndex >= (int)mScriptFilePaths.size())
      return;

   File scriptFile(mScriptFilePaths[mLoadScriptIndex]);
   if (!scriptFile.existsAsFile())
      return;

   std::unique_ptr<FileInputStream> input(scriptFile.createInputStream());
   if (!input->openedOk())
      return;

   std::string text = input->readString().toStdString();
   ofStringReplace(text, "\r", "");
   mCodeEntry->SetText(text);
   mLastPrint = "loaded " + scriptFile.getFileName().toStdString();
   mLastPrintTime = gTime;
}

void SardineModule::SaveScriptAs()
{
   File scriptsDir(ofToDataPath(kSardineScriptFolder));
   scriptsDir.createDirectory();

   FileChooser chooser("Save sardine script as...", scriptsDir.getChildFile("sardine.py"), "*.py", true, false, TheSynth->GetFileChooserParent());
   if (!chooser.browseForFileToSave(true))
      return;

   std::string path = chooser.getResult().getFullPathName().toStdString();
   File scriptFile(path);
   TemporaryFile tempFile(scriptFile);

   {
      FileOutputStream output(tempFile.getFile());
      if (!output.openedOk())
         return;

      output.writeText(mCodeEntry->GetText(false), false, false, nullptr);
      output.flush();

      if (output.getStatus().failed())
         return;
   }

   if (!tempFile.overwriteTargetFileWithTemporary())
      return;

   RefreshScriptFiles();

   for (size_t i = 0; i < mScriptFilePaths.size(); ++i)
   {
      if (mScriptFilePaths[i] == path)
      {
         mLoadScriptIndex = (int)i;
         break;
      }
   }

   mLastPrint = "saved " + scriptFile.getFileName().toStdString();
   mLastPrintTime = gTime;
}

void SardineModule::LoadLayout(const ofxJSONElement& moduleInfo)
{
   mModuleSaveData.LoadBool("execute_on_init", moduleInfo, false);
   mModuleSaveData.LoadBool("syntax_highlighting", moduleInfo, true);
   mModuleSaveData.LoadBool("sync_to_transport", moduleInfo, true);
   SetUpFromSaveData();
}

void SardineModule::SetUpFromSaveData()
{
   mExecuteOnInit = mModuleSaveData.GetBool("execute_on_init");
   mSyncToTransport = mModuleSaveData.GetBool("sync_to_transport");
   mCodeEntry->SetDoSyntaxHighlighting(mModuleSaveData.GetBool("syntax_highlighting"));
}

void SardineModule::SaveState(FileStreamOut& out)
{
   out << GetModuleSaveStateRev();
   IDrawableModule::SaveState(out);
   out << mWidth;
   out << mHeight;
}

void SardineModule::LoadState(FileStreamIn& in, int rev)
{
   IDrawableModule::LoadState(in, rev);

   float w, h;
   in >> w;
   in >> h;
   Resize(w, h);

   if (mExecuteOnInit)
      RunEditor(NextBufferTime(false));
}

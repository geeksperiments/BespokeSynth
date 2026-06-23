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

#pragma once

#include "ClickButton.h"
#include "IDrawableModule.h"
#include "CodeEntry.h"
#include "DropdownList.h"
#include "INoteSource.h"
#include "Slider.h"

class Checkbox;

class SardineModule : public IDrawableModule, public INoteSource, public IButtonListener, public ICodeEntryListener, public IFloatSliderListener, public IDropdownListener
{
public:
   SardineModule();
   ~SardineModule() override;
   static IDrawableModule* Create() { return new SardineModule(); }
   static bool AcceptsAudio() { return false; }
   static bool AcceptsNotes() { return false; }
   static bool AcceptsPulses() { return false; }

   void CreateUIControls() override;
   void Poll() override;

   void ButtonClicked(ClickButton* button, double time) override;
   void ExecuteCode() override;
   std::pair<int, int> ExecuteBlock(int lineStart, int lineEnd) override;
   void OnCodeUpdated() override {}
   void FloatSliderUpdated(FloatSlider* slider, float oldValue, double time) override {}
   void DropdownClicked(DropdownList* list) override;
   void DropdownUpdated(DropdownList* list, int oldValue, double time) override {}
   void PostRepatch(PatchCableSource* cableSource, bool fromUserClick) override;

   void SendNote(double delayMeasureTime, float pitch, float velocity, float lengthMeasureTime, float pan, int channel);
   void SendControl(double delayMeasureTime, int control, int value, int channel);
   void ScheduleCode(double delayMeasureTime, std::string code);
   void Stop();
   double GetCurrentMeasureTime() const;
   double GetCurrentRunTime() const { return mCurrentRunTime; }
   void PrintText(std::string text);

   bool IsEnabled() const override { return mEnabled; }
   void SetEnabled(bool on) override { mEnabled = on; }

   void SaveState(FileStreamOut& out) override;
   void LoadState(FileStreamIn& in, int rev) override;
   int GetModuleSaveStateRev() const override { return 1; }
   void LoadLayout(const ofxJSONElement& moduleInfo) override;
   void SetUpFromSaveData() override;

   static std::vector<SardineModule*> sSardineModules;

private:
   void RunCode(double time, const std::string& code);
   void RunEditor(double time);
   void RunEditorSynced(double time);
   void InstallPythonHelpers();
   void PlayNoteNow(double time, float pitch, float velocity, float pan, int channel, bool exactTiming);
   void ProcessScheduledEvents(double time);
   void ResetScheduledEvents();
   double GetNextBarStartTime(double time) const;
   void ScheduleCodeAtTime(double time, std::string code);
   void RefreshScriptFiles();
   void LoadSelectedScript();
   void SaveScriptAs();

   void DrawModule() override;
   bool IsResizable() const override { return true; }
   void Resize(float w, float h) override;

   struct ScheduledNote
   {
      double time{ -1 };
      float pitch{ 0 };
      float velocity{ 0 };
      float pan{ .5f };
   };

   struct ScheduledCode
   {
      double time{ -1 };
      std::string code;
   };

   CodeEntry* mCodeEntry{ nullptr };
   DropdownList* mLoadScriptSelector{ nullptr };
   ClickButton* mLoadScriptButton{ nullptr };
   ClickButton* mSaveScriptButton{ nullptr };
   ClickButton* mRunButton{ nullptr };
   ClickButton* mStopButton{ nullptr };
   FloatSlider* mASlider{ nullptr };
   FloatSlider* mBSlider{ nullptr };
   FloatSlider* mCSlider{ nullptr };
   FloatSlider* mDSlider{ nullptr };
   Checkbox* mSyncCheckbox{ nullptr };

   bool mEnabled{ true };
   bool mExecuteOnInit{ true };
   bool mSyncToTransport{ true };
   bool mPythonHelpersInstalled{ false };
   float mA{ 0 };
   float mB{ 0 };
   float mC{ 0 };
   float mD{ 0 };
   double mCurrentRunTime{ 0 };
   size_t mModuleIndex{ 0 };
   int mLoadScriptIndex{ -1 };
   std::vector<std::string> mScriptFilePaths;
   std::array<ScheduledNote, 512> mScheduledNotes;
   std::array<ScheduledCode, 128> mScheduledCode;
   std::string mLastError;
   std::string mLastPrint;
   double mLastPrintTime{ -1 };
};

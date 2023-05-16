/*  SPDX-License-Identifier: GPL-2.0-or-later */
/*!********************************************************************

  Audacity: A Digital Audio Editor

  ShareAudioDialog.cpp

  Dmitry Vedenko

**********************************************************************/
#include "ShareAudioDialog.h"

#include <cassert>

#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/gauge.h>
#include <wx/stattext.h>
#include <wx/statline.h>
#include <wx/textctrl.h>
#include <wx/radiobut.h>

#include "AllThemeResources.h"
#include "BasicUI.h"
#include "MemoryX.h"
#include "Project.h"
#include "ShuttleGui.h"
#include "Theme.h"
#include "Track.h"
#include "WaveTrack.h"

#include "ServiceConfig.h"
#include "OAuthService.h"
#include "UploadService.h"
#include "UserService.h"

#include "CloudExportersRegistry.h"
#include "CloudExporterPlugin.h"

#include "AuthorizationHandler.h"
#include "LinkAccountDialog.h"
#include "UserImage.h"

#include "CodeConversions.h"

#include "export/Export.h"
#include "AccessibleLinksFormatter.h"

#include "WindowAccessible.h"
#include "HelpSystem.h"

#ifdef HAS_CUSTOM_URL_HANDLING
#include "URLSchemesRegistry.h"
#endif

namespace cloud::audiocom
{
namespace
{
const wxSize avatarSize = { 32, 32 };

wxString GenerateTempPath(FileExtension extension)
{
   const auto tempPath = GetUploadTempPath();

   wxFileName fileName(
      tempPath,
      wxString::Format(
         "%lld", std::chrono::system_clock::now().time_since_epoch().count()),
      extension);

   fileName.Mkdir(0700, wxPATH_MKDIR_FULL);

   if (fileName.Exists())
   {
      if (!wxRemoveFile(fileName.GetFullPath()))
         return {};
   }

   return fileName.GetFullPath();
}

const auto publicLabelText = XO("Public");
const auto publicDescriptionText =
   XO("Anyone will be able to listen to this audio.");

const auto unlistedLabelText = XO("Unlisted");
const auto unlistedDescriptionText = XO(
   "Only you and people you share a link with will be able to listen to this audio.");

}

// A helper structures holds UploadService and UploadPromise
struct ShareAudioDialog::Services final
{
   UploadService uploadService;

   UploadOperationHandle uploadPromise;

   Services()
       : uploadService(GetServiceConfig(), GetOAuthService())
   {
   }
};

// Implementation of the ProgressDialog, which is not a dialog.
// Instead, progress is forwarded to the parent
struct ShareAudioDialog::ExportProgressHelper final :
    public BasicUI::ProgressDialog
{
   explicit ExportProgressHelper(ShareAudioDialog& parent)
       : mParent(parent)
   {
   }

   void Cancel()
   {
      mCancelled = true;
   }

   bool WasCancelled()
   {
      return mCancelled;
   }

   BasicUI::ProgressResult Poll(
      unsigned long long numerator, unsigned long long denominator,
      const TranslatableString&) override
   {
      mParent.UpdateProgress(numerator, denominator);

      const auto now = Clock::now();

      // Exporter polls in the main thread. To make the dialog responsive
      // periodic yielding is required
      if ((now - mLastYield > std::chrono::milliseconds(50)) || (numerator == denominator))
      {
         BasicUI::Yield();
         mLastYield = now;
      }

      return mCancelled ? BasicUI::ProgressResult::Cancelled :
                          BasicUI::ProgressResult::Success;
   }

   void SetMessage(const TranslatableString&) override
   {
   }

   void SetDialogTitle(const TranslatableString&) override
   {
   }

   void Reinit() override
   {
   }

   ShareAudioDialog& mParent;

   using Clock = std::chrono::steady_clock;
   Clock::time_point mLastYield;

   bool mCancelled { false };
};

ShareAudioDialog::ShareAudioDialog(AudacityProject& project, wxWindow* parent)
    : wxDialogWrapper(
         parent, wxID_ANY, XO("Share Audio"), wxDefaultPosition, { 480, 250 },
         wxDEFAULT_DIALOG_STYLE)
    , mProject(project)
    , mInitialStatePanel(*this) 
    , mServices(std::make_unique<Services>())
{
   GetAuthorizationHandler().PushSuppressDialogs();

   ShuttleGui s(this, eIsCreating);

   s.StartVerticalLay();
   {
      Populate(s);
   }
   s.EndVerticalLay();

   Layout();
   Fit();
   Centre();

   const auto size = GetSize();

   SetMinSize({ size.x, std::min(250, size.y) });
   SetMaxSize({ size.x, -1 });

   mContinueAction = [this]()
   {
      if (mInitialStatePanel.root->IsShown())
         StartUploadProcess();
   };

   Bind(
      wxEVT_CHAR_HOOK,
      [this](auto& evt)
      {
         if (!IsEscapeKey(evt))
         {
            evt.Skip();
            return;
         }

         OnCancel();
      });
}

ShareAudioDialog::~ShareAudioDialog()
{
   GetAuthorizationHandler().PopSuppressDialogs();
   // Clean up the temp file when the dialog is closed
   if (!mFilePath.empty() && wxFileExists(mFilePath))
      wxRemoveFile(mFilePath);
}

void ShareAudioDialog::Populate(ShuttleGui& s)
{
   mInitialStatePanel.PopulateInitialStatePanel(s);
   mProgressPanel.PopulateProgressPanel(s);

   s.StartHorizontalLay(wxEXPAND, 0);
   {
      s.StartInvisiblePanel(14);
      {
         s.SetBorder(2);
         s.StartHorizontalLay(wxEXPAND, 0);
         {
            s.AddSpace(0, 0, 1);

            mCancelButton = s.AddButton(XXO("&Cancel"));
            mCancelButton->Bind(wxEVT_BUTTON, [this](auto) { OnCancel(); });
            
            s.AddSpace(4, 0, 0);

            mContinueButton = s.AddButton(XXO("C&ontinue"));
            mContinueButton->Bind(wxEVT_BUTTON, [this](auto) { OnContinue(); });
            mContinueButton->Enable(mIsAuthorised);
         }
         s.EndHorizontalLay();
      }
      s.EndInvisiblePanel();
   }
   s.EndHorizontalLay();
}

void ShareAudioDialog::OnCancel()
{
   const auto hasExportStarted = mExportProgressHelper != nullptr;
   const auto hasUploadStarted = !!mServices->uploadPromise;

   if (mInProgress)
   {
      AudacityMessageDialog dlgMessage(
         this, XO("Are you sure you want to cancel?"), XO("Cancel upload to Audio.com"),
         wxYES_NO | wxICON_QUESTION | wxNO_DEFAULT | wxSTAY_ON_TOP);
      
      const auto result = dlgMessage.ShowModal();

      if (result != wxID_YES)
         return;

      // If export has started, notify it that it should be canceled
      if (mExportProgressHelper != nullptr)
         static_cast<ExportProgressHelper&>(*mExportProgressHelper).Cancel();
   }

   
   // If upload was started - ask it to discard the result.
   // The result should be discarded even after the upload has finished
   if (mServices->uploadPromise)
      mServices->uploadPromise->DiscardResult();

   EndModal(wxID_CANCEL);
}

void ShareAudioDialog::OnContinue()
{
   mContinueAction();
}

namespace
{
int CalculateChannels(const TrackList& trackList)
{
   for (auto track : trackList.Any<const WaveTrack>())
   {
      const auto channel = track->GetChannel();

      // Looks like we have a stereo track
      if (channel == Track::LeftChannel || channel == Track::RightChannel)
         return 2;

      const auto pan = track->GetPan();

      // We found a mono track with non zero pan
      // We treat equality in the same way Export
      if (pan != 0.0)
         return 2;
   }

   // All the wave tracks were mono with zero pan
   return 1;
}
}

wxString ShareAudioDialog::ExportProject()
{
   mExportProgressHelper = std::make_unique<ExportProgressHelper>(*this);

   auto exporter = CreatePreferredExporter(GetServiceConfig().GetPreferredAudioFormats(), mProject);

   if (!exporter)
      return {};

   const auto path = GenerateTempPath(exporter->GetFileExtension());

   if (path.empty())
      return {};
   

   SettingScope scope;
   exporter->OnBeforeExport();

   auto cleanupExporter = finally([&]() { exporter->OnAfterExport(); });

   Exporter e { const_cast<AudacityProject&>(mProject) };

   auto& tracks = TrackList::Get(mProject);

   const double t0 = 0.0;
   const double t1 = tracks.GetEndTime();

   const int nChannels = CalculateChannels(tracks);

   const bool success = e.Process(
      nChannels,                 // numChannels,
      exporter->GetExporterID(), // type,
      path,                      // full path,
      false,                     // selectedOnly,
      t0,                        // t0
      t1,                        // t1
      mExportProgressHelper      // progress dialog
   );

   if (!success && wxFileExists(path))
      // Try to remove the file if exporting has failed (or was canceled)
      wxRemoveFile(path);

   return success ? path : wxString {};
}

void ShareAudioDialog::StartUploadProcess()
{
   mInProgress = true;
   
   mInitialStatePanel.root->Hide();
   mProgressPanel.root->Show();
   
   mProgressPanel.info->Hide();

   mContinueButton->Hide();

   Layout();
   Fit();

   ResetProgress();

   mFilePath = ExportProject();

   if (mFilePath.empty())
   {
      if (!static_cast<ExportProgressHelper&>(*mExportProgressHelper)
              .WasCancelled())
      {
         HandleExportFailure();
      }

      return;
   }

   mProgressPanel.title->SetLabel(XO("Uploading audio...").Translation());
   ResetProgress();

   mServices->uploadPromise = mServices->uploadService.Upload(
      mFilePath,
      mProject.GetProjectName(),
      false,
      [this](const auto& result)
      {
         CallAfter(
            [this, result]()
            {
               mInProgress = false;
               
               if (result.result == UploadOperationCompleted::Result::Success)
               {
                  // Success indicates that UploadSuccessfulPayload is in the payload
                  assert(std::holds_alternative<UploadSuccessfulPayload>(result.payload));

                  if (
                     auto payload =
                        std::get_if<UploadSuccessfulPayload>(&result.payload))
                     HandleUploadSucceeded(*payload);
                  else
                     HandleUploadSucceeded({});
                  
               }
               else if (
                  result.result != UploadOperationCompleted::Result::Aborted)
               {
                  if (
                     auto payload =
                        std::get_if<UploadFailedPayload>(&result.payload))
                     HandleUploadFailed(*payload);
                  else
                     HandleUploadFailed({});
               }
            });
      },
      [this](auto current, auto total)
      {
         CallAfter(
            [this, current, total]()
            {
               UpdateProgress(current, total);
            });
      });
}

void ShareAudioDialog::HandleUploadSucceeded(
   const UploadSuccessfulPayload& payload)
{
   mProgressPanel.timePanel->Hide();
   mProgressPanel.title->SetLabel(XO("Upload complete!").Translation());
   mProgressPanel.info->Show();

   mProgressPanel.info->SetLabel(
      "By pressing continue, you will be taken to audio.com and given a shareable link.");
   mProgressPanel.info->Wrap(mProgressPanel.root->GetSize().GetWidth());

   mContinueAction = [this, slug = std::string(payload.audioSlug)]()
   {
      EndModal(wxID_CLOSE);
      auto url = wxString::Format(
         "https://audio.com/%s/%s/edit", GetUserService().GetUserSlug(),
         audacity::ToWXString(slug));
      
      OpenInDefaultBrowser(url);
   };

   mContinueButton->Show();

   Layout();
   Fit();
}

void ShareAudioDialog::HandleUploadFailed(const UploadFailedPayload& payload)
{
   EndModal(wxID_ABORT);

   TranslatableString message;

   if (payload.status == 401)
   {
      message = XO(
         "We are unable to upload this file. Please try again and make sure to link to your audio.com account before uploading.");
   }
   else
   {
      auto details = payload.message;

      for (auto& err : payload.additionalErrors)
         details += " " + err.second;
      
      message = XO("Error: %s").Format(details);
   }

   BasicUI::ShowErrorDialog(
      {}, XO("Upload error"),
      message,
      {},
      BasicUI::ErrorDialogOptions { BasicUI::ErrorDialogType::ModalError });
         
}

void ShareAudioDialog::HandleExportFailure()
{
   EndModal(wxID_ABORT);

   BasicUI::ShowErrorDialog(
      {}, XO("Export error"),
      XO("We are unable to prepare this file for uploading."), {},
      BasicUI::ErrorDialogOptions { BasicUI::ErrorDialogType::ModalError });
}

void ShareAudioDialog::ResetProgress()
{
   mStageStartTime = Clock::now();
   mLastUIUpdateTime = mStageStartTime;

   mProgressPanel.elapsedTime->SetLabel(" 00:00:00");
   mProgressPanel.remainingTime->SetLabel(" 00:00:00");
   mProgressPanel.progress->SetValue(0);

   mLastProgressValue = 0;

   BasicUI::Yield();
}

namespace
{
void SetTimeLabel(wxStaticText* label, std::chrono::milliseconds time)
{
   wxTimeSpan tsElapsed(0, 0, 0, time.count());

   label->SetLabel(tsElapsed.Format(wxT(" %H:%M:%S")));
   label->SetName(label->GetLabel());
   label->Update();
}
}

void ShareAudioDialog::UpdateProgress(uint64_t current, uint64_t total)
{
   using namespace std::chrono;

   const auto now = Clock::now();

   if (current == 0)
      return;

   if (current > total)
      current = total;

   if (mLastProgressValue != current)
   {
      constexpr int scale = 10000;

      mLastProgressValue = static_cast<int>(current);

      mProgressPanel.progress->SetRange(scale);
      mProgressPanel.progress->SetValue((current * scale) / total);

      if (current == total && mServices->uploadPromise)
      {
         mProgressPanel.timePanel->Hide();
         mProgressPanel.title->SetLabel(XO("Finalizing upload...").Translation());
      }
   }

   const auto elapsedSinceUIUpdate = now - mLastUIUpdateTime;

   constexpr auto uiUpdateTimeout = 500ms;

   if (elapsedSinceUIUpdate < uiUpdateTimeout && current < total)
      return;

   mLastUIUpdateTime = now;

   const auto elapsed = duration_cast<milliseconds>(now - mStageStartTime);

   SetTimeLabel(mProgressPanel.elapsedTime, elapsed);

   const auto estimate = elapsed * total / current;
   const auto remains = estimate - elapsed;

   SetTimeLabel(
      mProgressPanel.remainingTime,
      std::chrono::duration_cast<std::chrono::milliseconds>(remains));
}

ShareAudioDialog::InitialStatePanel::InitialStatePanel(ShareAudioDialog& parent)
    : parent { parent }
    , mUserDataChangedSubscription(
         GetUserService().Subscribe([this](const auto&) { UpdateUserData(); }))
{
}

void ShareAudioDialog::InitialStatePanel::PopulateInitialStatePanel(
   ShuttleGui& s)
{
   root = s.StartInvisiblePanel();
   s.StartVerticalLay(wxEXPAND, 1);
   {
      s.SetBorder(16);

      s.StartHorizontalLay(wxEXPAND, 0);
      {
         avatar = safenew UserImage(s.GetParent(), avatarSize);

         s.AddWindow(avatar);

         s.StartVerticalLay(wxEXPAND, 1);
         {
            s.SetBorder(0);
            s.AddSpace(0, 0, 1);
            name = s.AddVariableText(XO("Anonymous"));
            s.AddSpace(0, 0, 1);
         }
         s.EndVerticalLay();

         s.AddSpace(0, 0, 1);

         s.StartVerticalLay(wxEXPAND, 1);
         {
            s.AddSpace(0, 0, 1);

            s.SetBorder(16);
            oauthButton = s.AddButton(XXO("&Link Account"));
            oauthButton->Bind(
               wxEVT_BUTTON, [this](auto) { OnLinkButtonPressed(); });
            s.AddSpace(0, 0, 1);
         }
         s.EndVerticalLay();
      }
      s.EndHorizontalLay();

      s.SetBorder(0);

      s.AddWindow(safenew wxStaticLine { s.GetParent() }, wxEXPAND);

      s.AddSpace(16);
      s.StartInvisiblePanel();
      {
         anonInfoPanel = s.StartInvisiblePanel();
         {
            s.SetBorder(30);
            
            AccessibleLinksFormatter privacyPolicy(XO(
               "Your audio will be uploaded to our sharing service: %s,%%which requires a free account to use."));

            privacyPolicy.FormatLink(
               L"%s", XO("audio.com"), "https://audio.com");

            privacyPolicy.FormatLink(
               L"%%", TranslatableString {},
               AccessibleLinksFormatter::LinkClickedHandler {});

            privacyPolicy.Populate(s);
         }
         s.EndInvisiblePanel();

         authorizedInfoPanel = s.StartInvisiblePanel();
         s.StartHorizontalLay(wxEXPAND, 1);
         {
            s.AddSpace(30);
            s.AddFixedText(XO("Press \"Continue\" to upload to audio.com"));
         }
         s.EndHorizontalLay();
         s.EndInvisiblePanel();
      }
      s.EndInvisiblePanel();
   }
   s.EndVerticalLay();
   s.EndInvisiblePanel();

   UpdateUserData();
}

void ShareAudioDialog::InitialStatePanel::UpdateUserData()
{
   auto rootParent = root->GetParent();
   rootParent->Freeze();
   
   auto layoutUpdater = finally(
      [rootParent = root->GetParent(), this]()
      {
         oauthButton->Fit();
         rootParent->Fit();
         rootParent->Layout();

         rootParent->Thaw();
      });

   auto& oauthService = GetOAuthService();

   if (!oauthService.HasRefreshToken())
   {
      SetAnonymousState();
      return;
   }

   if (!oauthService.HasAccessToken())
      oauthService.ValidateAuth({});

   auto& userService = GetUserService();

   if (userService.GetUserSlug().empty())
   {
      SetAnonymousState();
      return;
   }

   const auto displayName = userService.GetDisplayName();

   if (!displayName.empty())
      name->SetLabel(displayName);

   const auto avatarPath = userService.GetAvatarPath();

   if (!avatarPath.empty())
      avatar->SetBitmap(avatarPath);
   else
      avatar->SetBitmap(theTheme.Bitmap(bmpAnonymousUser));

   oauthButton->SetLabel(XXO("&Unlink Account").Translation());

   parent.mIsAuthorised = true;

   anonInfoPanel->Hide();
   authorizedInfoPanel->Show();
  
   if (parent.mContinueButton != nullptr)
      parent.mContinueButton->Enable();
}

void ShareAudioDialog::InitialStatePanel::OnLinkButtonPressed()
{
   auto& oauthService = GetOAuthService();

   if (oauthService.HasAccessToken())
      oauthService.UnlinkAccount();
   else
   {
      OpenInDefaultBrowser(
         { audacity::ToWXString(GetServiceConfig().GetOAuthLoginPage()) });

#ifdef HAS_CUSTOM_URL_HANDLING
      if (!URLSchemesRegistry::Get().IsURLHandlingSupported())
#endif
      {
         LinkAccountDialog dlg(root);
         dlg.ShowModal();
      }
   }
}

void ShareAudioDialog::InitialStatePanel::SetAnonymousState()
{
   parent.mIsAuthorised = false;
   
   name->SetLabel(XO("Anonymous").Translation());
   avatar->SetBitmap(theTheme.Bitmap(bmpAnonymousUser));
   oauthButton->SetLabel(XXO("&Link Account").Translation());

   anonInfoPanel->Show();
   authorizedInfoPanel->Hide();

   if (parent.mContinueButton != nullptr)
      parent.mContinueButton->Enable(false);
}

void ShareAudioDialog::ProgressPanel::PopulateProgressPanel(ShuttleGui& s)
{
   root = s.StartInvisiblePanel(16);
   root->Hide();
   s.StartVerticalLay(wxEXPAND, 1);
   {
      s.SetBorder(0);

      title = s.AddVariableText(XO("Preparing audio..."));
      s.AddSpace(0, 16, 0);

      progress = safenew wxGauge { s.GetParent(), wxID_ANY, 100 };
      s.AddWindow(progress, wxEXPAND);

      timePanel = s.StartInvisiblePanel();
      {
         s.AddSpace(0, 16, 0);

         s.StartWrapLay();
         {
            s.AddFixedText(XO("Elapsed Time:"));
            elapsedTime = s.AddVariableText(Verbatim(" 00:00:00"));
         }
         s.EndWrapLay();

         s.StartWrapLay();
         {
            s.AddFixedText(XO("Remaining Time:"));
            remainingTime = s.AddVariableText(Verbatim(" 00:00:00"));
         }
         s.EndWrapLay();
      }
      s.EndInvisiblePanel();

      s.AddSpace(0, 16, 0);
      
      info = s.AddVariableText(publicDescriptionText);
   }
   
   s.EndVerticalLay();
   s.EndInvisiblePanel();

   wxFont font = elapsedTime->GetFont();
   font.MakeBold();

   elapsedTime->SetFont(font);
   remainingTime->SetFont(font);
}
} // namespace cloud::audiocom

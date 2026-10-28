#include "app/ui/Strings.hpp"

#include <array>
#include <utility>

namespace sloptube::app {
namespace {

constexpr std::size_t kLanguageCount = 2;

struct Translation {
    Text id;
    std::array<std::string_view, kLanguageCount> text; // Indexed by Language.
};

constexpr std::array<std::pair<std::string_view, Language>, kLanguageCount> kLanguageCodes{{
    {"en", Language::English},
    {"fr", Language::French},
}};

constexpr std::array<Translation, std::to_underlying(Text::Count)> kTranslations{{
    {Text::Home, {"Home", "Accueil"}},
    {Text::Subscriptions, {"Subscriptions", "Abonnements"}},
    {Text::You, {"You", "Vous"}},
    {Text::History, {"History", "Historique"}},
    {Text::Playlists, {"Playlists", "Playlists"}},
    {Text::SearchHint, {"Search", "Rechercher"}},
    {Text::SignIn, {"Sign in", "Se connecter"}},
    {Text::SignOut, {"Sign out", "Se déconnecter"}},
    {Text::SignInPrompt,
     {"Sign in with your Google account to get your history, subscriptions and playlists.",
      "Connecte-toi avec ton compte Google pour retrouver ton historique, tes abonnements et "
      "tes playlists."}},
    {Text::SignInStep,
     {"On your phone or computer, go to", "Sur ton téléphone ou ordinateur, va sur"}},
    {Text::SignInCode, {"and enter this code:", "puis saisis ce code :"}},
    {Text::OpenPage, {"Open page", "Ouvrir la page"}},
    {Text::Waiting, {"Waiting for approval…", "En attente de validation…"}},
    {Text::SignInFailed, {"Sign-in was cancelled or expired.", "Connexion annulée ou expirée."}},
    {Text::Retry, {"Retry", "Réessayer"}},
    {Text::Nothing, {"Nothing to show yet.", "Rien à afficher pour l'instant."}},
    {Text::PlaybackFailed, {"This video can't be played.", "Impossible de lire cette vidéo."}},
    {Text::Subscribe, {"Subscribe", "S'abonner"}},
    {Text::Subscribed, {"Subscribed", "Abonné"}},
    {Text::Comments, {"Comments", "Commentaires"}},
    {Text::TopComments, {"Top", "Les plus populaires"}},
    {Text::NewestComments, {"Newest", "Les plus récents"}},
    {Text::AddComment, {"Add a comment…", "Commenter…"}},
    {Text::AddReply, {"Add a reply…", "Répondez…"}},
    {Text::Replies, {"Replies", "Réponses"}},
    {Text::ReplyCount, {"{} replies", "{} réponses"}},
    {Text::OneReply, {"1 reply", "1 réponse"}},
    {Text::ReadMore, {"Read more", "Lire la suite"}},
    {Text::More, {"…more", "…plus"}},
    {Text::Description, {"Description", "Description"}},
    {Text::Likes, {"Likes", "J'aime"}},
    {Text::Views, {"Views", "Vues"}},
    {Text::CommentsOff, {"Comments are turned off.", "Les commentaires sont désactivés."}},
    {Text::SignInToAct,
     {"Sign in to like, subscribe and comment.",
      "Connecte-toi pour aimer, t'abonner et commenter."}},
    {Text::ActionFailed,
     {"Couldn't complete that action.", "Impossible d'effectuer cette action."}},
    {Text::CreateUnavailable,
     {"Uploading isn't available in SlopTube.",
      "La mise en ligne n'est pas disponible dans SlopTube."}},
    {Text::Dislike, {"Dislike", "Je n'aime pas"}},
    {Text::Share, {"Share", "Partager"}},
    {Text::Save, {"Save", "Enregistrer"}},
    {Text::SaveVideoTo, {"Save video to...", "Enregistrer la vidéo dans..."}},
    {Text::NewPlaylist, {"New playlist", "Nouvelle playlist"}},
    {Text::PlaylistTitleHint, {"Choose a title", "Choisissez un titre"}},
    {Text::CreatePlaylist, {"Create", "Créer"}},
    {Text::PrivacyPrivate, {"Private", "Privée"}},
    {Text::PrivacyUnlisted, {"Unlisted", "Non répertoriée"}},
    {Text::PrivacyPublic, {"Public", "Publique"}},
    {Text::SavedTo, {"Saved to {}", "Enregistrée dans {}"}},
    {Text::RemovedFrom, {"Removed from {}", "Supprimée de {}"}},
    {Text::Download, {"Download", "Télécharger"}},
    {Text::Downloads, {"Downloads", "Téléchargements"}},
    {Text::Downloaded, {"Downloaded", "Téléchargée"}},
    {Text::DownloadQueued, {"Waiting to download", "En attente de téléchargement"}},
    {Text::DownloadFailed,
     {"Download failed. Tap to retry.", "Échec du téléchargement. Appuyez pour réessayer."}},
    {Text::DownloadVideo, {"Download quality", "Qualité du téléchargement"}},
    {Text::DeleteDownload, {"Delete from downloads", "Supprimer des téléchargements"}},
    {Text::NoDownloads,
     {"Videos you download appear here, even without a connection.",
      "Les vidéos que vous téléchargez s'affichent ici, même sans connexion."}},
    {Text::Offline, {"You're offline", "Vous êtes hors connexion"}},
    {Text::OfflineHint,
     {"Watch your downloads while you have no connection.",
      "Regardez vos téléchargements en attendant d'avoir une connexion."}},
    {Text::GoToDownloads, {"Go to downloads", "Accéder aux téléchargements"}},
    {Text::Megabytes, {"{} MB", "{} Mo"}},
    {Text::Percent, {"{}%", "{} %"}},
    {Text::Posted, {"Posted", "Publié"}},
    {Text::Accounts, {"Accounts", "Comptes"}},
    {Text::SwitchAccount, {"Switch account", "Changer de compte"}},
    {Text::AddAccount, {"Add account", "Ajouter un compte"}},
    {Text::ViewChannel, {"View channel", "Afficher la chaîne"}},
    {Text::Shorts, {"Shorts", "Shorts"}},
    {Text::Post, {"Post", "Post"}},
    {Text::PostActionUnavailable,
     {"Not available for posts yet", "Pas encore disponible pour les posts"}},
    {Text::Quality, {"Quality", "Qualité"}},
    {Text::AutoQuality, {"Auto", "Auto"}},
    {Text::Subtitles, {"Subtitles", "Sous-titres"}},
    {Text::SubtitlesOff, {"Off", "Désactivés"}},
    {Text::AutoGenerated, {"auto-generated", "générés automatiquement"}},
    {Text::AutoTranslate, {"Auto-translate", "Traduire automatiquement"}},
    {Text::SubtitlesFailed, {"Subtitles unavailable", "Sous-titres indisponibles"}},
    {Text::TranslationFailed,
     {"Auto-translation unavailable", "Traduction automatique indisponible"}},
    {Text::HeldSpeed, {"2x", "x 2"}},
    {Text::ZoomedToFill, {"Zoomed to fill", "Vidéo agrandie pour occuper tout l'écran"}},
    {Text::GoToChannel, {"Go to channel", "Accéder à la chaîne"}},
    {Text::ReleaseToCancel, {"Release to cancel", "Relâchez pour annuler"}},
    {Text::PullUpForPrecise,
     {"Pull up for precise seeking", "Faites glisser vers le haut pour une recherche précise"}},
    {Text::Live, {"LIVE", "EN DIRECT"}},
}};

// Catch a missing or misordered row at compile time rather than showing the wrong text.
consteval bool translationsInOrder() {
    for (std::size_t i = 0; i < kTranslations.size(); ++i) {
        if (std::to_underlying(kTranslations[i].id) != i) {
            return false;
        }
    }
    return true;
}

static_assert(translationsInOrder(), "kTranslations must list every Text in declaration order");

Language g_language = Language::English;

} // namespace

Language languageFor(std::string_view isoCode) {
    for (const auto& [code, language] : kLanguageCodes) {
        if (code == isoCode) {
            return language;
        }
    }
    return Language::English;
}

void setLanguage(Language language) {
    g_language = language;
}

std::string_view tr(Text text) {
    return kTranslations[std::to_underlying(text)].text[std::to_underlying(g_language)];
}

} // namespace sloptube::app

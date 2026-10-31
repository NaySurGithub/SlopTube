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
    {Text::Settings, {"Settings", "Paramètres"}},
    {Text::BackgroundPlay, {"Background play", "Lecture en arrière-plan"}},
    {Text::BackgroundPlayDetail,
     {"Keep playing when you leave the app or lock the screen",
      "Continuer la lecture hors de l'application ou écran verrouillé"}},
    {Text::PictureInPicture, {"Picture-in-picture", "Picture-in-picture"}},
    {Text::PictureInPictureDetail,
     {"Shrink the video to a window when you leave the app",
      "Réduire la vidéo dans une fenêtre en quittant l'application"}},
    {Text::Autoplay, {"Autoplay", "Lecture automatique"}},
    {Text::AutoplayDetail,
     {"Play the next video when one ends", "Lire la vidéo suivante à la fin de la vidéo"}},
    {Text::UploadNotifications, {"New upload notifications", "Notifications des nouvelles vidéos"}},
    {Text::UploadNotificationsDetail,
     {"From your subscriptions, checked about every hour",
      "De vos abonnements, vérifiées environ toutes les heures"}},
    {Text::ClearSearchHistory, {"Clear search history", "Effacer l'historique des recherches"}},
    {Text::ClearSearchHistoryDetail,
     {"Your searches are kept on this phone only",
      "Vos recherches ne sont gardées que sur ce téléphone"}},
    {Text::SearchHistoryEmpty, {"No searches yet", "Aucune recherche pour l'instant"}},
    {Text::SearchHistoryCleared, {"Search history cleared", "Historique des recherches effacé"}},
    {Text::UpNextIn, {"Up next in {}", "À suivre dans {}"}},
    {Text::Cancel, {"Cancel", "Annuler"}},
    {Text::PlayNow, {"Play now", "Lire"}},
    {Text::PlaybackSpeed, {"Playback speed", "Vitesse de lecture"}},
    {Text::NormalSpeed, {"Normal", "Normale"}},
    {Text::LiveChat, {"Live chat", "Chat en direct"}},
    {Text::LiveChatUnavailable,
     {"Chat is unavailable for this stream", "Le chat n'est pas disponible pour ce direct"}},
    {Text::FilterSort, {"Prioritize", "Prioriser"}},
    {Text::FilterPopularity, {"Popularity", "Popularité"}},
    {Text::FilterUploadDate, {"Upload date", "Date d'ajout"}},
    {Text::FilterLastHour, {"Last hour", "Dernière heure"}},
    {Text::FilterToday, {"Today", "Aujourd'hui"}},
    {Text::FilterThisWeek, {"This week", "Cette semaine"}},
    {Text::FilterThisMonth, {"This month", "Ce mois-ci"}},
    {Text::FilterThisYear, {"This year", "Cette année"}},
    {Text::FilterType, {"Type", "Type"}},
    {Text::FilterVideos, {"Videos", "Vidéos"}},
    {Text::FilterChannels, {"Channels", "Chaînes"}},
    {Text::FilterMovies, {"Movies", "Films"}},
    {Text::FilterDuration, {"Duration", "Durée"}},
    {Text::FilterUnderThree, {"Under 3 minutes", "Moins de 3 minutes"}},
    {Text::FilterThreeToTwenty, {"3 - 20 minutes", "3 à 20 minutes"}},
    {Text::FilterOverTwenty, {"Over 20 minutes", "Plus de 20 minutes"}},
    {Text::FilterFeatures, {"Features", "Caractéristiques"}},
    {Text::Filter4K, {"4K", "4K"}},
    {Text::FilterHd, {"HD", "HD"}},
    {Text::ClearFilters, {"Clear filters", "Effacer les filtres"}},
    {Text::LiveTab, {"Live", "En direct"}},
    {Text::InterfaceSize, {"Text and button size", "Taille du texte et des boutons"}},
    {Text::InterfaceSizeDetail,
     {"Makes everything bigger and easier to tap",
      "Agrandit tout pour lire et toucher plus facilement"}},
    {Text::SizeNormal, {"Normal", "Normale"}},
    {Text::SizeLarge, {"Large", "Grande"}},
    {Text::SizeExtraLarge, {"Extra large", "Très grande"}},
    {Text::NoConnection,
     {"No internet connection. Check Wi-Fi or mobile data, then try again.",
      "Pas de connexion Internet. Vérifiez le Wi-Fi ou les données mobiles, puis réessayez."}},
    {Text::SomethingWentWrong,
     {"Something went wrong. Please try again.", "Un problème est survenu. Veuillez réessayer."}},
    {Text::CannotPlay,
     {"This video can't be played right now.", "Impossible de lire cette vidéo pour le moment."}},
    {Text::UpdateAvailable, {"Update available", "Mise à jour disponible"}},
    {Text::UpdateIntro,
     {"Version {} of SlopTube is ready. You have version {}.",
      "La version {} de SlopTube est prête. Vous avez la version {}."}},
    {Text::UpdateNow, {"Update now", "Mettre à jour"}},
    {Text::UpdateInstall, {"Install", "Installer"}},
    {Text::Later, {"Later", "Plus tard"}},
    {Text::UpdateDownloading, {"Downloading… {}%", "Téléchargement… {} %"}},
    {Text::UpdateFollowSteps,
     {"Follow the steps on screen to install.", "Suivez les étapes à l'écran pour installer."}},
    {Text::UpdateNotInstalled,
     {"The update was not installed.", "La mise à jour n'a pas été installée."}},
    {Text::UpToDate, {"SlopTube is up to date", "SlopTube est à jour"}},
    {Text::CheckUpdatesAutomatically,
     {"Check for updates automatically", "Rechercher les mises à jour automatiquement"}},
    {Text::CheckUpdatesDetail,
     {"When the app opens, a new version is offered",
      "À l'ouverture, une nouvelle version vous est proposée"}},
    {Text::CheckForUpdates, {"Check for updates", "Rechercher une mise à jour"}},
    {Text::CheckingForUpdates, {"Checking for updates…", "Recherche d'une mise à jour…"}},
    {Text::CurrentVersion, {"Version {}", "Version {}"}},
    {Text::Notifications, {"Notifications", "Notifications"}},
    {Text::Library, {"Library", "Bibliothèque"}},
    {Text::Recent, {"Recent", "Récents"}},
    {Text::SaveToPlaylist, {"Save to playlist", "Enregistrer dans une playlist"}},
    {Text::FilterAll, {"All", "Tous"}},
    {Text::AllChannels, {"All", "Tout"}},
    {Text::AllSubscriptions, {"All subscriptions", "Tous les abonnements"}},
    {Text::NotificationsAll, {"All", "Toutes"}},
    {Text::NotificationsToday, {"Today", "Aujourd'hui"}},
    {Text::NotificationsThisWeek, {"This week", "Cette semaine"}},
    {Text::NotificationsOlder, {"Older", "Plus anciennes"}},
    {Text::UploadedPrefix, {"Uploaded: ", "Mise en ligne : "}},
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

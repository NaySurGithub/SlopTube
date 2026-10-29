// Points the download button straight at the newest release's APK, with its version and size.
// Without this (no JavaScript, API unreachable or rate-limited), it opens the releases page.
(async () => {
  const button = document.getElementById("download-button");
  const detail = document.getElementById("download-detail");
  try {
    const response = await fetch(
      "https://api.github.com/repos/NaySurGithub/SlopTube/releases/latest",
      { headers: { Accept: "application/vnd.github+json" } },
    );
    if (!response.ok) {
      return;
    }
    const release = await response.json();
    const apk = release.assets.find((asset) => asset.name.endsWith(".apk"));
    if (!apk) {
      return;
    }
    button.href = apk.browser_download_url;
    const megabytes = (apk.size / (1024 * 1024)).toFixed(1);
    detail.textContent = `Version ${release.tag_name.replace(/^v/, "")} · ${megabytes} MB`;
  } catch {
    // The releases page is the fallback.
  }
})();

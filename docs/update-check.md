# Update check

Merik Studio looks at startup (and when you press **Check for updates**) whether a newer version exists. It asks one small file on the website:

    https://merik.app/studio/latest.json

```json
{ "version": "1.0.1", "url": "https://merik.app/studio/#download" }
```

- `version`: the newest released version (semantic version, e.g. `1.0.1` or `1.1.0-beta.1`).
- `url`: the page that opens when you press Download. Only addresses on `https://merik.app/` are accepted; anything else falls back to the download page.

Nothing is sent about you or your computer besides the normal HTTPS request. The check can be switched off in **Settings → General**.

**Publishing a release:** after the installers are on the website, put the new version in `site/studio/latest.json` (website repository) and deploy. Until that file says the new version, nobody is told about it.

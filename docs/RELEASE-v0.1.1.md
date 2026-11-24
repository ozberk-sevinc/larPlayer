# LARPlayer v0.1.1 — proposed security release

The deck, library, playlists, search, hotkeys and audio controls remain available. This release changes setup and packaging.

- The launcher gives your Soloist key to the Spotify-signed `setup-api-key` command through stdin and starts the receiver without `-k`. Existing installs keep their Windows Credential Manager key. A Soloist build without `setup-api-key` will fail closed; update from Spotify's official download page.
- New and existing Soloist executables must have a valid Windows code signature identifying Spotify AB. Downloads still use Spotify's official HTTPS URL. Archive extraction rejects traversal, links, extra executables and oversized payloads before executing the signed binary.
- The classic ncspot executable is fetched during the build from its official v1.5.1 GitHub release and its fixed upstream ZIP digest is checked. The previously committed, unverified executable is removed from the source tree. Build failure or unavailable upstream verification stops the package.
- The release ZIP has an exact file manifest, scans for private key blocks and credential assignments, and publishes a SHA-256 digest in CI.

**Key rotation:** v0.1.0 passed your key on Soloist's command line while it ran. If you used that version, rotate the key at the Spotify Soloist dashboard as a precaution, especially on a shared PC or if you sent diagnostic process listings to anyone. Then run `LARPlayer.exe key` to store the new key. v0.1.1 cannot remove copies captured earlier.

**Remaining local exposure:** The live deck still needs Soloist's unauthenticated WebSocket server. It binds only to `127.0.0.1` on a random port; other local processes or browser pages able to connect to localhost may observe or control playback. Spotify must add server-side client authentication and browser Origin checks, or a protected local IPC transport, to close this gap. Do not describe this release as fully secure.

**Verification before publishing:** attach only the clean GitHub Actions artifact from the v0.1.1 commit, record its SHA-256 from the companion digest artifact, and perform a signed-in playback check. Do not change the v0.1.0 tag or asset.

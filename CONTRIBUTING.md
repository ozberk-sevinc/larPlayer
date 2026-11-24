# Contributing to Groove Dial

Build on Windows x64 with the Visual Studio C++ workload and Windows SDK using `./scripts/build.ps1`. Run `./scripts/smoke.ps1` before submitting a change. The GitHub Actions workflow performs the same checks on each push and pull request.

Playback changes need a signed-in Spotify Premium account for a manual end-to-end check. Include the steps and observed result in the pull request. Local library search uses the ncspot cache, while streaming and playback controls use Spotify Soloist.

Keep Spotify API keys, account profiles, session files, caches and unredacted logs out of issues and pull requests. The app reads a user's own key from Windows Credential Manager and downloads Soloist from Spotify for that user.

The Groove Dial source uses the MIT license. The optional ncspot executable has its own BSD 2-Clause notice in `engine/LICENSE-ncspot.txt`.

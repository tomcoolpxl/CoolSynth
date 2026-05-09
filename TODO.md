# TODO

- [ ] Add a Windows CI workflow for clean-checkout configure and build.
- [ ] Run standalone and VST3 targets in CI.
- [ ] Reuse the documented JUCE bootstrap path in CI.
- [ ] Publish enough CI logs to review build results without relying on release artifacts.
- [ ] Verify the CI workflow succeeds on a branch.
- [ ] Document CI expectations in `README.md`.
- [ ] Add a tag-triggered Windows release workflow that creates or updates a GitHub release with generated release notes and uploads standalone and VST3 assets.
- [ ] Prove the CI and CD paths end-to-end by running one manual validation workflow and one disposable prerelease-tag release workflow.

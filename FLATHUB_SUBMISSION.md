# Flathub Submission Guide for Mudlet

This document describes the Flatpak/Flathub implementation that has been completed and the steps needed to submit Mudlet to Flathub.

## What Has Been Done

### 1. Flatpak Build Infrastructure (Completed ✅)

The following files have been added to the Mudlet repository:

- **`.github/workflows/build-mudlet-flatpak.yml`** - GitHub Actions workflow for building Flatpak packages
- **`CI/org.mudlet.mudlet.yml`** - Flatpak manifest with all dependencies configured
- **`CI/lua-5.1.5-so.patch`** - Required patch for building Lua as a shared library
- **`org.mudlet.mudlet.metainfo.xml`** - AppStream metadata file (required for Flathub)
- **`flathub.json`** - Flathub build configuration
- **`src/CMakeLists.txt`** - Updated with install rules for desktop files, icons, and metainfo
- **Desktop integration files** - `mudlet.desktop`, `mudlet.svg`, `mudlet.png` (already present)

These changes are committed to: `claude/complete-flat-feature-011CULwAeRqGZpC3yM1eGE1W`

### 2. Flathub Submission Files (Prepared ✅)

A complete Flathub submission has been prepared in `/home/user/flathub/` with:

- `org.mudlet.mudlet.yml` - Flatpak manifest pointing to Mudlet 4.19.1 release
- `org.mudlet.mudlet.metainfo.xml` - AppStream metadata
- `lua-5.1.5-so.patch` - Lua build patch
- `flathub.json` - Architecture configuration

A git patch file has been created: **`0001-Add-org.mudlet.mudlet.patch`**

## Prerequisites Before Submission

### Required GitHub Secrets

The Flatpak build workflow requires these secrets to be configured in the GitHub repository settings:

- `GPG_KEY_GREP` - GPG key grip for signing
- `GPG_PRIVATE_KEY` - Base64-encoded GPG private key
- `GPG_PASSPHRASE` - Passphrase for the GPG key
- `GPG_KEY_ID` - GPG key ID for signing

### Merge to Main Repository

The Flatpak changes should ideally be merged into the main Mudlet repository (`Mudlet/Mudlet`) before Flathub submission, though initial submission can proceed with the current release tag.

## Steps to Submit to Flathub

### Step 1: Fork the Flathub Repository

1. Go to https://github.com/flathub/flathub
2. Click "Fork" to create your own fork
3. Make sure to fork from the `new-pr` branch

### Step 2: Clone Your Fork

```bash
git clone -b new-pr https://github.com/YOUR_USERNAME/flathub.git
cd flathub
```

### Step 3: Apply the Prepared Submission

You can either:

**Option A: Apply the patch file**
```bash
git am /home/user/Mudlet/0001-Add-org.mudlet.mudlet.patch
```

**Option B: Manually copy files from `/home/user/flathub/`**
```bash
cp /home/user/flathub/org.mudlet.mudlet.yml .
cp /home/user/flathub/org.mudlet.mudlet.metainfo.xml .
cp /home/user/flathub/lua-5.1.5-so.patch .
cp /home/user/flathub/flathub.json .
git add .
git commit -m "Add org.mudlet.mudlet"
```

### Step 4: Push to Your Fork

```bash
git push origin new-pr
```

### Step 5: Create Pull Request

1. Go to your fork on GitHub: `https://github.com/YOUR_USERNAME/flathub`
2. Click "Pull Request"
3. **Important**: Make sure the base branch is `flathub:new-pr` (NOT `master`)
4. Title: `Add org.mudlet.mudlet`
5. Add any relevant description about Mudlet
6. Submit the pull request

### Step 6: Respond to Review

Flathub maintainers will review your submission. They may:
- Request changes to the manifest
- Ask for additional metadata
- Suggest improvements
- Test the build

Be prepared to make updates based on their feedback.

## Testing Locally (Recommended)

Before submitting, test the build locally:

```bash
# Install flatpak-builder if not already installed
sudo apt install flatpak-builder  # Debian/Ubuntu
# or
sudo dnf install flatpak-builder  # Fedora

# Build and test
cd /home/user/flathub
flatpak-builder --force-clean --install-deps-from=flathub build-dir org.mudlet.mudlet.yml --user --install
flatpak run org.mudlet.mudlet
```

## After Approval

Once approved:
1. Flathub will create a new repository at `https://github.com/flathub/org.mudlet.mudlet`
2. You'll receive write access to this repository
3. Future updates are done by pushing to that repository
4. The app will be available on Flathub for all users

## Notes

- The current submission uses Mudlet 4.19.1 (latest stable release)
- The metainfo file includes the AppStream metadata required for software centers
- The manifest includes all necessary dependencies (Lua 5.1, libzip, pugixml, yajl, etc.)
- Once PR #7116 is merged, the manifest can be updated to use the development branch

## Resources

- [Flathub App Submission](https://docs.flathub.org/docs/for-app-authors/submission)
- [Flathub Requirements](https://docs.flathub.org/docs/for-app-authors/requirements)
- [Flatpak Documentation](https://docs.flatpak.org/)
- [AppStream Specification](https://www.freedesktop.org/software/appstream/docs/)

## Contact

For questions about this submission, refer to PR #7116 in the Mudlet repository.

# Building Mudlet Flatpak Locally - Complete Guide

This guide will walk you through building and testing the Mudlet Flatpak on your local machine.

## Prerequisites

### 1. Install Flatpak and Flatpak-Builder

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install flatpak flatpak-builder
```

**Fedora:**
```bash
sudo dnf install flatpak flatpak-builder
```

**Arch Linux:**
```bash
sudo pacman -S flatpak flatpak-builder
```

### 2. Add Flathub Repository

```bash
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
```

### 3. Install KDE Runtime and SDK

Since Mudlet uses the KDE runtime (version 6.8), you need to install it:

```bash
# Install the runtime (required to run the app)
flatpak install flathub org.kde.Platform//6.8

# Install the SDK (required to build the app)
flatpak install flathub org.kde.Sdk//6.8
```

## Building from Mudlet Repository

### Option 1: Build from Your Local Mudlet Repository

```bash
# Navigate to your Mudlet source directory
cd ~/Programs/vadi2-Mudlet

# Create a build directory
mkdir -p flatpak-build

# Build the Flatpak
flatpak-builder --force-clean \
  --install-deps-from=flathub \
  --repo=repo \
  flatpak-build \
  CI/org.mudlet.mudlet.yml
```

**What this does:**
- `--force-clean` - Removes previous build directory before starting
- `--install-deps-from=flathub` - Automatically installs any missing dependencies
- `--repo=repo` - Creates a local Flatpak repository in the `repo` directory
- `flatpak-build` - Directory where the build happens
- `CI/org.mudlet.mudlet.yml` - The Flatpak manifest

### Option 2: Build from Flathub Submission Files

```bash
# Navigate to your flathub fork
cd ~/Programs/flathub

# Create a build directory
mkdir -p build-dir

# Build the Flatpak
flatpak-builder --force-clean \
  --install-deps-from=flathub \
  --repo=repo \
  build-dir \
  org.mudlet.mudlet.yml
```

## Installing the Built Flatpak

After building, install it to your user account:

```bash
# From the directory where you built (has the 'repo' folder)
flatpak --user remote-add --no-gpg-verify mudlet-local repo

# Install the app
flatpak --user install mudlet-local org.mudlet.mudlet
```

## Running the Flatpak

```bash
# Run the installed Flatpak
flatpak run org.mudlet.mudlet

# Or launch from your application menu
# It should appear as "Mudlet" in the Games category
```

## Quick Build + Install + Run (One-Liner)

If you want to build and immediately install/run for testing:

```bash
# From Mudlet repository
cd ~/Programs/vadi2-Mudlet

flatpak-builder --force-clean --user --install --install-deps-from=flathub build-dir CI/org.mudlet.mudlet.yml && flatpak run org.mudlet.mudlet
```

**What this does:**
- `--user` - Install to user directory (no sudo needed)
- `--install` - Automatically install after building
- Then runs the app immediately

## Debugging Build Issues

### Enable Verbose Output

```bash
flatpak-builder --force-clean --verbose --install-deps-from=flathub build-dir CI/org.mudlet.mudlet.yml
```

### Build a Specific Module Only

If one module is failing, you can build up to that point:

```bash
flatpak-builder --force-clean --stop-at=mudlet build-dir CI/org.mudlet.mudlet.yml
```

### Get a Shell in the Build Environment

```bash
# Build the environment
flatpak-builder --force-clean build-dir CI/org.mudlet.mudlet.yml

# Get a shell to debug
flatpak-builder --run build-dir CI/org.mudlet.mudlet.yml bash
```

## Testing the Flatpak

### 1. Check if it Launches

```bash
flatpak run org.mudlet.mudlet
```

### 2. Check File Access

Test that Mudlet can access your home directory (it should, based on `--filesystem=home` permission):

```bash
# In Mudlet, try to save/load profiles
# They should save to ~/.local/share/Mudlet or similar
```

### 3. Check Network Connectivity

```bash
# Try connecting to a MUD server within Mudlet
# Network should work due to `--share=network` permission
```

### 4. Verify Desktop Integration

```bash
# Check if .desktop file is installed
flatpak run --command=ls org.mudlet.mudlet /app/share/applications/

# Check if icons are installed
flatpak run --command=ls org.mudlet.mudlet /app/share/icons/hicolor/
```

## Uninstalling

```bash
# Uninstall the Flatpak
flatpak uninstall org.mudlet.mudlet

# Remove the local repository
flatpak --user remote-delete mudlet-local

# Clean up build directory
rm -rf build-dir repo flatpak-build
```

## Common Issues and Solutions

### Issue: "Failed to init: Unable to find sdk org.kde.Sdk version 6.8"

**Solution:**
```bash
flatpak install flathub org.kde.Sdk//6.8
```

### Issue: "error: No remote refs found similar to 'flathub'"

**Solution:**
```bash
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
```

### Issue: Build takes a very long time

**Solution:**
- This is normal for first build (downloads and compiles all dependencies)
- Subsequent builds are faster due to caching
- Use `--ccache` flag to speed up C++ compilation:
  ```bash
  flatpak-builder --ccache --force-clean build-dir CI/org.mudlet.mudlet.yml
  ```

### Issue: "error: While pulling runtime/org.kde.Platform/x86_64/6.8"

**Solution:**
```bash
# Make sure you have the runtime installed
flatpak install flathub org.kde.Platform//6.8 org.kde.Sdk//6.8
```

## Validating the Flatpak Manifest

Before building, you can validate the manifest:

```bash
# Install flatpak linter
flatpak install flathub org.flatpak.Builder

# Validate the manifest
flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest CI/org.mudlet.mudlet.yml

# Validate after building
flatpak run --command=flatpak-builder-lint org.flatpak.Builder repo repo
```

## Creating a Flatpak Bundle for Distribution

If you want to create a `.flatpak` bundle file to share:

```bash
# Build with a repository
flatpak-builder --force-clean --repo=repo build-dir CI/org.mudlet.mudlet.yml

# Create a bundle
flatpak build-bundle repo mudlet.flatpak org.mudlet.mudlet

# Others can install it with:
# flatpak install mudlet.flatpak
```

## Build Time Estimate

First build: **30-60 minutes** (downloads and compiles all dependencies)
Subsequent builds: **5-15 minutes** (only recompiles changed parts)

## Next Steps

After successfully building locally:
1. Test all major features
2. Check for any runtime errors in the console
3. If everything works, your Flathub submission is ready!

## Getting Help

- Flatpak documentation: https://docs.flatpak.org/
- Flatpak builder docs: https://docs.flatpak.org/en/latest/flatpak-builder.html
- Flathub Matrix chat: #flathub:matrix.org

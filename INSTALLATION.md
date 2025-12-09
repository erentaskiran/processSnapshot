# Installation Guide

This guide will walk you through setting up the development environment and installing the State Checkpoint System project.

## 📋 Prerequisites

This project requires **Arch Linux** environment. We provide a pre-configured Arch Linux VM for easy setup.

## 🚀 Installation Steps

### Step 1: Download Arch Linux VM

Download the pre-configured Arch Linux environment archive:

**Download Link:** [arch.zip](https://drive.google.com/file/d/1hLlEgRtGzY1hLI_UVXxlPyi54dCjAiei/view?usp=share_link)

The archive includes:
- Pre-configured Arch Linux VM
- Required development tools
- Startup scripts for different platforms

### Step 2: Extract the Archive

After downloading, extract the archive:

**Windows:**
```powershell
# Using File Explorer: Right-click → Extract All
# Or using PowerShell:
Expand-Archive -Path arch.zip -DestinationPath .
```

**Linux:**
```bash
unzip arch.zip
```

**macOS:**
```bash
unzip arch.zip
# Or double-click the file in Finder
```

### Step 3: Start the Arch Linux VM

Navigate to the extracted `arch` directory and run the appropriate startup script:

#### For Windows Users

Open PowerShell in the `arch` directory and run:

```powershell
.\start_arch.ps1
```

> **Note:** If you encounter an execution policy error, run:
> ```powershell
> Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
> ```

#### For Linux Users

Open a terminal in the `arch` directory and run:

```bash
# Make the script executable (first time only)
chmod +x start_arch.sh

# Start the VM
./start_arch.sh
```

#### For macOS Users

Open a terminal in the `arch` directory and run:

```bash
# Make the script executable (first time only)
chmod +x start_arch.sh

# Start the VM
./start_arch.sh
```

### Step 4: Verify VM is Running

Once the VM starts, you should see an Arch Linux terminal. Verify the environment:

```bash
# Check Arch Linux version
cat /etc/os-release

# Check if required tools are installed
gcc --version
cmake --version
git --version
```

Expected output:
- GCC version 12 or higher
- CMake version 3.20 or higher
- Git version 2.x

### Step 5: Clone the Project Repository

Inside the Arch Linux VM, clone the project repository:

```bash
# Clone from GitHub
git clone https://github.com/erentaskiran/processSnapshot.git

# Navigate to the project directory
cd processSnapshot
```

Alternatively, if you're working with a specific branch:

```bash
# Clone and checkout specific branch
git clone -b your-branch-name https://github.com/erentaskiran/processSnapshot.git
cd processSnapshot
```

### Step 6: Build the Project

Now build the project using CMake:

```bash
# Create a build directory
mkdir -p build && cd build

# Configure the project
cmake ..

# Build the project (use all available CPU cores)
make -j$(nproc)
```

Build output will be in the `build/bin/` directory.

### Step 7: Verify Installation

Run the test suite to ensure everything is working correctly:

```bash
# Run all tests
./bin/checkpoint_tests

# Expected output: All tests should pass
# [==========] X tests from Y test suites ran.
# [  PASSED  ] X tests.
```

Run a simple example:

```bash
# Run simple checkpoint example
./bin/simple_example

# Expected output: Checkpoint creation and restoration demo
```

## 🎉 Installation Complete!

You're now ready to start working with the State Checkpoint System!

## 📖 Next Steps

1. Read the [GETTING_STARTED.md](GETTING_STARTED.md) guide to understand the project structure
2. Explore the examples in `build/bin/`:
   - `simple_example` - Basic checkpoint usage
   - `process_demo` - Process simulation with checkpoints
   - `auto_save_example` - Automatic checkpoint creation
   - `real_process_demo` - Real process checkpointing
3. Check out the [README.md](README.md) for feature overview
4. Start experimenting with the code!

## 🛠️ Troubleshooting

### VM Won't Start

**Issue:** Script fails to execute or VM doesn't start

**Solutions:**
- **Windows:** Ensure Hyper-V or VirtualBox is installed
- **Linux/macOS:** Check if you have virtualization enabled in BIOS
- **All platforms:** Make sure you have enough RAM (minimum 2GB recommended)

### Build Fails with Compiler Errors

**Issue:** `cmake` or `make` fails with C++ errors

**Solutions:**
```bash
# Update system packages
sudo pacman -Syu

# Install/update required tools
sudo pacman -S gcc cmake make git

# Try building again
cd build
rm -rf *
cmake ..
make -j$(nproc)
```

### Git Clone Fails

**Issue:** Cannot connect to GitHub or authentication fails

**Solutions:**
```bash
# Check internet connection
ping github.com

# If authentication required, set up SSH key or use personal access token
# Or clone using HTTPS with credentials
git clone https://github.com/erentaskiran/processSnapshot.git
```

### Tests Fail

**Issue:** Some or all tests fail after building

**Solutions:**
```bash
# Clean build and rebuild
cd build
make clean
make -j$(nproc)

# Run tests with verbose output
./bin/checkpoint_tests --gtest_print_time=1

# Check if checkpoint directories exist
ls -la ../checkpoints/
ls -la ../simple_checkpoints/
```

### Permission Denied Errors

**Issue:** Cannot create files or directories

**Solutions:**
```bash
# Check permissions
ls -la

# Fix ownership if needed (run as regular user, not root)
# Make sure you're in your home directory or a writable location
cd ~
git clone https://github.com/erentaskiran/processSnapshot.git
cd processSnapshot
```

## 🔧 Manual Setup (Alternative)

If you cannot use the provided VM, you can set up the environment manually on an existing Arch Linux system:

### Install Dependencies

```bash
# Update system
sudo pacman -Syu

# Install build tools
sudo pacman -S base-devel cmake git

# Install GCC (if not already installed)
sudo pacman -S gcc

# Optional: Install debugging tools
sudo pacman -S gdb valgrind
```

### Clone and Build

```bash
# Clone repository
git clone https://github.com/erentaskiran/processSnapshot.git
cd processSnapshot

# Build
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# Test
./bin/checkpoint_tests
```

## 📞 Getting Help

If you encounter issues not covered in this guide:

1. Check the [GETTING_STARTED.md](GETTING_STARTED.md) for project-specific information
2. Review the [README.md](README.md) for feature documentation
3. Look at existing issues on GitHub
4. Contact your instructor or project maintainer

## 📝 Summary

Complete installation checklist:

- [ ] Download arch.zip
- [ ] Extract the archive
- [ ] Run start_arch.ps1 (Windows) or start_arch.sh (Linux/macOS)
- [ ] Verify VM is running
- [ ] Clone the repository: `git clone https://github.com/erentaskiran/processSnapshot.git`
- [ ] Build the project: `mkdir build && cd build && cmake .. && make`
- [ ] Run tests: `./bin/checkpoint_tests`
- [ ] Run examples: `./bin/simple_example`
- [ ] Read GETTING_STARTED.md

**Happy coding! 🚀**

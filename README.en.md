<div align="center">
  <img src="assets/usb-eject-source.png" width="80" alt="Win11Usb USB icon">
  <h1>Win11Usb</h1>
  <p><strong>Turn a Windows ISO into an installation USB.</strong></p>
  <p>Native C++ / Win32 · No .NET · No PowerShell · One portable EXE<br>Full-capacity FAT32 · Automatic large-WIM splitting · Offline creation</p>
  <p>
    <a href="https://github.com/kaixinit/Win11Usb/releases/tag/v0.2.0-preview.14"><img src="https://img.shields.io/badge/Native_Pre--release-v0.2.0--preview.14-2563eb?style=flat-square" alt="Native pre-release v0.2.0-preview.14"></a>
    <img src="https://img.shields.io/badge/Windows-10%20%2F%2011%20x64-0078d4?style=flat-square" alt="Targets Windows 10 / 11 x64">
    <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-16a34a?style=flat-square" alt="Original project code is MIT licensed"></a>
  </p>
  <p>
    <a href="https://github.com/kaixinit/Win11Usb/releases/download/v0.2.0-preview.14/Win11Usb-v0.2.0-preview.14-windows-x64.exe"><strong>↓ Download native EXE · Windows x64</strong></a>
    &nbsp;·&nbsp; <a href="https://github.com/kaixinit/Win11Usb/releases/tag/v0.2.0-preview.14">Release notes</a>
    &nbsp;·&nbsp; <a href="README.md">中文</a>
  </p>
</div>

> **Creating media erases every partition and file on the selected USB disk.** Back up first and check the disk number, model and capacity in the final confirmation. The native version is a pre-release; beta.3 remains available separately.

## Follow the job in one window

Choose an ISO and a USB disk, then follow six stages with overall progress, the current file and scrolling logs. Batched updates reduce flicker during copying.

![Native application preview with six stages, overall/file progress and live logs](docs/screenshots/native/running.png)

*Screenshots show the actual native interface in a read-only preview, using sample devices and simulated progress. They are not evidence of real-media creation or a successful boot.*

<details>
<summary>Main window and formatting confirmation</summary>

**Choose a Windows ISO, physical USB disk and filesystem**

![Native main window in read-only preview with sample data](docs/screenshots/native/ready.png)

**Confirm the whole-disk erase separately; Cancel is the default**

![Full-capacity FAT32 confirmation in read-only preview with sample data](docs/screenshots/native/format-fat32.png)

</details>

## Features

| Feature | What it does |
| --- | --- |
| **Native, without .NET or PowerShell** | C++ / Win32 interface and backend; no CLR, WPF or PowerShell host. No separate .NET, Visual C++ runtime or WebView installation. |
| **One portable EXE, offline creation** | Download and run. Choose a local Windows ISO; the program mounts it read-only and identifies the image. Creation needs no network. |
| **Full-capacity FAT32 on large USB disks** | Erase an NTFS or multipartition USB disk and rebuild one full-capacity FAT32 partition, instead of using only about 32 GB. A complete real-media log covers a 57.8 GiB Kingston disk. |
| **Automatic large-WIM splitting** | Split `install.wim` locally into SWM files when it exceeds 4,294,967,295 bytes, then copy them. FAT32 and NTFS follow the same rule. |
| **Compare copied contents** | Flush caches, check file lengths and compare source and destination contents block by block. Recheck essential x64 UEFI files; verification shows progress. |
| **Clear results and cleanup** | Optionally remove this job's temporary SWM files and safely eject the USB disk and ISO. An eject failure is reported separately from successful creation. |

The full-capacity FAT32 workflow supports **8 GiB–2 TiB** USB disks, allowing for normal partition alignment and filesystem overhead. **This erases and rebuilds the disk; it does not preserve existing files.**

## Downloads and versions

| Download | Purpose |
| --- | --- |
| **[Native single-file EXE](https://github.com/kaixinit/Win11Usb/releases/download/v0.2.0-preview.14/Win11Usb-v0.2.0-preview.14-windows-x64.exe)** | `v0.2.0-preview.14`, Windows x64. Run directly, without installation. |
| [Native companion ZIP](https://github.com/kaixinit/Win11Usb/releases/download/v0.2.0-preview.14/Win11Usb-v0.2.0-preview.14-windows-x64.zip) | The same EXE, documentation and third-party source/licenses for archiving or sharing. |
| [Project source ZIP](https://github.com/kaixinit/Win11Usb/releases/download/v0.2.0-preview.14/Win11Usb-v0.2.0-preview.14-source.zip) · [SHA-256 checksums](https://github.com/kaixinit/Win11Usb/releases/download/v0.2.0-preview.14/SHA256SUMS.txt) | Build, review or verify release files. |
| [Retained beta.3 release](https://github.com/kaixinit/Win11Usb/releases/tag/v0.1.0-beta.3) | The earlier C# / WPF / PowerShell version, with its own files and release history. |

**The native release does not automatically overwrite beta.3.** They have separate version numbers, tags and EXE filenames; keep them in separate folders. There is no automatic updater. Do not run both versions against the same USB disk at the same time.

## Create installation media

Prepare a local Windows 10/11 x64 installation ISO and a backed-up USB disk of at least 8 GiB, large enough for the installation files.

1. Run the native EXE and accept the Windows administrator prompt.
2. Select the ISO and verify the physical USB disk. Usually prefer FAT32.
3. Start creation. The program mounts and checks the ISO; check the final disk identity, erase scope and filesystem before confirming formatting.
4. Splitting, copying and content verification continue automatically. Follow progress and logs in the same window.
5. After completion, choose whether to clean temporary SWM files and safely eject the USB disk and ISO.

Some UEFI firmware cannot boot directly from NTFS; Win11Usb does not silently switch to NTFS. Splitting uses DISM `/FileSize:4000`; the image determines the number of parts, and each part's size and numbering are checked. Content verification reads the source and destination again; it is not a drive health or bad-sector test.

<details>
<summary>System requirements, temporary files and compatibility</summary>

- Targets **Windows 10/11 x64** and x64 UEFI installation media. Legacy BIOS, ARM64 and WinPE are not claimed as supported.
- Removing .NET and PowerShell dependencies does not make every stripped Windows installation compatible. The interface needs Win32 desktop, GDI+, SetupAPI and storage drivers. Creation uses Windows ISO/WIM APIs, `dism.exe` for splitting and `format.com` for NTFS. Required components must remain available.
- Single-file distribution means one EXE to download. A job still creates logs, temporary SWM files and an extracted independent FAT32 tool with its corresponding source/licenses. Local storage space is required.
- System/boot, offline and read-only disks are excluded. Device identity, layout and paths are rechecked; the ISO, program and temporary directory must not be on the target. These checks do not replace backups or manual verification.
- Identification reads installation image index 1; a multi-edition ISO can still install other editions. Filenames may help identify a release, but cannot prove that a same-family ISO was not renamed incorrectly.
- Force-closing can leave media requiring recreation and an ISO still mounted. The job directory holds logs; use the window's diagnostic export when troubleshooting.
- This is not a multiboot, Windows download or activation tool.

</details>

## Validation so far

The native version passed **962 automated assertions**, plus a production UI test with **5,000 log lines and 10,000 progress updates**. PE checks confirm a native x64 executable without CLR. Defender file scans found no threats with real-time protection enabled.

A complete log supplied after preview.14 delivery records **a 57.8 GiB Kingston disk formatted as full-capacity FAT32 with 32 KiB clusters and 57.7 GiB available, 2 SWM files, 1,067 files copied and content-verified, temporary SWM cleanup, and successful USB and ISO eject.** The log does not embed a version or EXE hash; the result applies to that configuration.

Actual UEFI boot, real-media NTFS and more devices/systems remain unverified. A file scan does not establish clean full-workflow dynamic behavior or universal antivirus compatibility. The EXE is unsigned, so this remains a **Pre-release**. [Native acceptance record](native/ACCEPTANCE.md) · [USB eject fix and retest](native/USB-EJECT-preview14.md) · [Historical beta.3 validation](docs/VALIDATION.md)

<details>
<summary>Developers: build the native version</summary>

Native source lives in `native/`. Use llvm-mingw from a command prompt at the repository root:

```bat
native\build.cmd "C:\path\to\llvm-mingw\bin" full
native\test.cmd "C:\path\to\llvm-mingw\bin"
native\test-ui.cmd "C:\path\to\llvm-mingw\bin"
```

The creation build produces `dist\Win11Usb-NativeReview-v0.2.0-preview.14.exe`; release packaging uses the versioned filename above. Omit `full` to build the read-only variant without the creation backend, with device diagnostics, `--eject-report` and sample interface previews.

Building does not require PowerShell either. The earlier `src/`, `build/` and scripts remain for beta.3. [Native architecture, dependencies and build details](native/README.md)

</details>

## Feedback and licenses

[Report an issue](https://github.com/kaixinit/Win11Usb/issues) with the version, Windows build, failed stage and redacted logs. USB and firmware boot reports help expand the compatibility record. Check personal paths and device information before posting logs.

If Win11Usb is useful to you, a **Star** helps others discover it.

Original code: [MIT](LICENSE). The independent FAT32 tool and corresponding source retain GPL terms; interface icons retain Apache 2.0 terms, and static runtimes retain their own licenses. See [third-party notices](THIRD_PARTY_NOTICES.md) and [native runtime licenses](native/third_party/RUNTIME-LICENSES.txt). No Windows ISO or installation binaries are distributed with the project.

# A virtual camera cannot ship as MSIX

English | [한국어](msix-limitation.ko.md)

Investigated 2026-08-24 · Windows 11 (26100 / 26200), Windows SDK 10.0.26100

## Conclusion

**A Windows virtual camera does not work from an MSIX package alone.**
It builds, it packages, it installs — and then the camera never turns on.
You need a classic installer (MSI/EXE) that registers under `HKLM` the way
`regsvr32` does.

This does not block Microsoft Store distribution. Since 2021 the Store accepts
`.msi`/`.exe` installers as well as MSIX. That path, however, **requires a code
signing certificate of your own**.

## Why

The code that generates frames (a DLL) is not run by your app. The **Windows
Camera Frame Server** loads it into its own process, and that service runs
inside `svchost.exe` as **LOCAL SERVICE**.

```
my app (vcamhost.exe)
   │  MFCreateVirtualCamera("make a camera with this CLSID")
   │  the app's job ends here - it is not in the video path
   ▼
Windows Camera Frame Server   ← svchost.exe / LOCAL SERVICE
   │  1. looks the CLSID up in the registry
   │  2. CoCreateInstance → loads our DLL into itself
   │  3. asks for a frame every 33ms
   ▼
Camera app / Chrome / Zoom / …
```

The point that matters: **the process loading the DLL is a system service
running as a different account.**

| Registration | Where it lands | Can LOCAL SERVICE see it? |
|---|---|---|
| `regsvr32` (MSI/EXE) | `HKLM` | **Yes** |
| MSIX `com4:InProcessServer` | per-user package catalogue | **No** |

MSIX registration is per-user by nature. The service account never had the
package registered, so it cannot resolve a package-relative path.

This matches what Microsoft's own documentation says about media sources: they
must be registered in `HKLM`, and "registration in `HKCU` is not possible
because multiple processes load them". MSIX's per-user registration is in the
same position as `HKCU`.

## The experiment

A minimal MSIX package with its own CLSID, registering the media source through
`com4:InProcessServer` (loose registration in developer mode, no signature
needed). The CLSID was kept separate so it could not interfere with the working
installed build.

| # | Check | Result |
|---|---|---|
| 1 | C++ build, MSIX manifest validation, package registration | **all succeeded** |
| 2 | `MFCreateVirtualCamera` | **succeeded** |
| 3 | `IMFVirtualCamera::Start()` | **failed — `0x80070003` (path not found)** |
| 4 | Camera appears in device lists | did not appear |
| 5 | Move the package to `C:\Users\Public`, re-register | **same failure** → not a folder permission problem |
| 6 | An **ordinary process outside the package** calls `CoCreateInstance(INPROC)` on the same CLSID | **succeeded (S_OK)** |

Step 3 is decisive because the error is **not** `REGDB_E_CLASSNOTREG`. The
registration was found; the DLL path could not be opened.

Step 6 settles the cause. **An ordinary process in the same user session loads
that CLSID perfectly well.** The registration is not missing — it is visible to
exactly one principal, the user who registered it. The only party that fails is
the Frame Server, which is a different account.

## Not tested

A properly signed MSIX installed into `C:\Program Files\WindowsApps` was not
tried. Two reasons to expect the same result:

- what blocks it is **per-user registration**, not the file's location
- the file-accessibility hypothesis was already ruled out by step 5

To nail it down completely, build a properly signed MSIX with a self-signed
certificate and repeat the test.

## So how does it ship

| | MSIX | MSI / EXE |
|---|---|---|
| Virtual camera works | **no** | yes (the same way Microsoft's own sample does) |
| Code signing | Microsoft signs it (free) | **buy a certificate and sign it yourself** |
| Store listing | yes | yes (submit a link to the installer) |
| Install and update | handled by the OS | your problem |

A virtual camera product has no choice but MSI/EXE, and at that moment code
signing becomes your own responsibility.
**The one path where signing is free — Store MSIX — is precisely the path this
kind of product cannot use.**

What this project actually chose is in the README's
[code signing policy](../README.md#code-signing-policy).

A signed file that also carries a **timestamp stays valid after the certificate
expires**. New builds still need new signatures, so for anything that ships
updates, renewal is effectively mandatory.

## References

- [Windows-Camera VirtualCamera sample](https://github.com/microsoft/Windows-Camera/tree/master/Samples/VirtualCamera) — Microsoft's official sample, distributed as MSI
- [com4:ComServer manifest schema](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-com4-comserver)
- [Code signing options for Windows app developers](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options)
- [Choose a distribution path for your Windows app](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/choose-distribution-path)

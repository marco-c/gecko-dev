/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SandboxPolicyContent_h
#define mozilla_SandboxPolicyContent_h

#define MAX_CONTENT_TESTING_READ_PATHS 4

namespace mozilla {

static const char SandboxPolicyContent[] = R"SANDBOX_LITERAL(
  (version 1)

  (define should-log (param "SHOULD_LOG"))
  (define sandbox-level-1 (param "SANDBOX_LEVEL_1"))
  (define sandbox-level-2 (param "SANDBOX_LEVEL_2"))
  (define sandbox-level-3 (param "SANDBOX_LEVEL_3"))
  (define macosMinorVersion (string->number (param "MAC_OS_MINOR")))
  (define appPath (param "APP_PATH"))
  (define hasProfileDir (param "HAS_SANDBOXED_PROFILE"))
  (define profileDir (param "PROFILE_DIR"))
  (define hasWindowServer (param "HAS_WINDOW_SERVER"))
  (define home-path (param "HOME_PATH"))
  (define debugWriteDir (param "DEBUG_WRITE_DIR"))
  (define testingReadPath1 (param "TESTING_READ_PATH1"))
  (define testingReadPath2 (param "TESTING_READ_PATH2"))
  (define testingReadPath3 (param "TESTING_READ_PATH3"))
  (define testingReadPath4 (param "TESTING_READ_PATH4"))
  (define crashPort (param "CRASH_PORT"))

  (define (moz-deny feature)
    (if (string=? should-log "TRUE")
      (deny feature)
      (deny feature (with no-log))))

  (moz-deny default)
  ; These are not included in (deny default)
  (moz-deny process-info*)
  ; This isn't available in some older macOS releases.
  (if (defined? 'nvram*)
    (moz-deny nvram*))
  ; The next two properties both require macOS 10.10+
  (if (defined? 'iokit-get-properties)
    (moz-deny iokit-get-properties))
  (if (defined? 'file-map-executable)
    (moz-deny file-map-executable))

  (if (string=? should-log "TRUE")
    (debug deny))

  (if (defined? 'file-map-executable)
    (allow file-map-executable file-read*
      (subpath "/System")
      (subpath "/usr/lib")
      (subpath "/Library/GPUBundles")
      (subpath appPath))
    (allow file-read*
        (subpath "/System")
        (subpath "/usr/lib")
        (subpath "/Library/GPUBundles")
        (subpath appPath)))

  ; Allow read access to standard system paths.
  (allow file-read*
    (require-all (file-mode #o0004)
      (require-any
        (subpath "/Library/Filesystems/NetFSPlugins")
        (subpath "/usr/share"))))

  ; Top-level directory metadata access (bug 1404298)
  (allow file-read-metadata (regex #"^/[^/]+$"))

  (allow file-read-metadata
    (literal "/private/etc/localtime")
    (regex #"^/private/tmp/KSInstallAction\."))

  ; Allow read access to standard special files.
  (allow file-read*
    (literal "/dev/autofs_nowait")
    (literal "/dev/random")
    (literal "/dev/urandom"))

  (allow file-read*
    file-write-data
    (literal "/dev/null")
    (literal "/dev/zero"))

  (allow file-read*
    file-write-data
    file-ioctl
    (literal "/dev/dtracehelper"))

  ; Needed for things like getpriority()/setpriority()
  (allow process-info-pidinfo process-info-setcontrol (target self))

  ; macOS 10.9 does not support the |sysctl-name| predicate, so unfortunately
  ; we need to allow all sysctl-reads there.
  (if (= macosMinorVersion 9)
    (allow sysctl-read)
    (allow sysctl-read
      (sysctl-name-regex #"^sysctl\.")
      (sysctl-name "kern.ostype")
      (sysctl-name "kern.osversion")
      (sysctl-name "kern.osrelease")
      (sysctl-name "kern.version")
      (sysctl-name "kern.tcsm_available")
      (sysctl-name "kern.tcsm_enable")
      ; TODO: remove "kern.hostname". Without it the tests hang, but the hostname
      ; is arguably sensitive information, so we should see what can be done about
      ; removing it.
      (sysctl-name "kern.hostname")
      (sysctl-name "hw.machine")
      (sysctl-name "hw.model")
      (sysctl-name "hw.ncpu")
      (sysctl-name "hw.activecpu")
      (sysctl-name "hw.byteorder")
      (sysctl-name "hw.pagesize_compat")
      (sysctl-name "hw.logicalcpu_max")
      (sysctl-name "hw.physicalcpu_max")
      (sysctl-name "hw.busfrequency_compat")
      (sysctl-name "hw.busfrequency_max")
      (sysctl-name "hw.cpufrequency")
      (sysctl-name "hw.cpufrequency_compat")
      (sysctl-name "hw.cpufrequency_max")
      (sysctl-name "hw.l2cachesize")
      (sysctl-name "hw.l3cachesize")
      (sysctl-name "hw.cachelinesize")
      (sysctl-name "hw.cachelinesize_compat")
      (sysctl-name "hw.tbfrequency_compat")
      (sysctl-name "hw.vectorunit")
      (sysctl-name "hw.optional.sse2")
      (sysctl-name "hw.optional.sse3")
      (sysctl-name "hw.optional.sse4_1")
      (sysctl-name "hw.optional.sse4_2")
      (sysctl-name "hw.optional.avx1_0")
      (sysctl-name "hw.optional.avx2_0")
      (sysctl-name "machdep.cpu.vendor")
      (sysctl-name "machdep.cpu.family")
      (sysctl-name "machdep.cpu.model")
      (sysctl-name "machdep.cpu.stepping")
      (sysctl-name "debug.intel.gstLevelGST")
      (sysctl-name "debug.intel.gstLoaderControl")))
  (if (> macosMinorVersion 9)
    (allow sysctl-write
      (sysctl-name "kern.tcsm_enable")))

  (define (home-regex home-relative-regex)
    (regex (string-append "^" (regex-quote home-path) home-relative-regex)))
  (define (home-subpath home-relative-subpath)
    (subpath (string-append home-path home-relative-subpath)))
  (define (home-literal home-relative-literal)
    (literal (string-append home-path home-relative-literal)))

  (define (profile-subpath profile-relative-subpath)
    (subpath (string-append profileDir profile-relative-subpath)))

  (define (allow-shared-list domain)
    (allow file-read*
           (home-regex (string-append "/Library/Preferences/" (regex-quote domain)))))

  (allow ipc-posix-shm-read-data ipc-posix-shm-write-data
    (ipc-posix-name-regex #"^CFPBS:"))

  (allow signal (target self))
  (if (string? crashPort)
    (allow mach-lookup (global-name crashPort)))
  (if (string=? hasWindowServer "TRUE")
    (allow mach-lookup (global-name "com.apple.windowserver.active")))
  (allow mach-lookup
    (global-name "com.apple.CoreServices.coreservicesd")
    (global-name "com.apple.coreservices.launchservicesd")
    (global-name "com.apple.lsd.mapdb"))

  (if (>= macosMinorVersion 13)
    (allow mach-lookup
      ; bug 1392988
      (xpc-service-name "com.apple.coremedia.videodecoder")
      (xpc-service-name "com.apple.coremedia.videoencoder")))

; bug 1312273
  (if (= macosMinorVersion 9)
     (allow mach-lookup (global-name "com.apple.xpcd")))

  (allow iokit-open
     (iokit-user-client-class "IOHIDParamUserClient"))

  ; Only supported on macOS 10.10+
  (if (defined? 'iokit-get-properties)
    (allow iokit-get-properties
      (iokit-property "board-id")
      (iokit-property "vendor-id")
      (iokit-property "device-id")
      (iokit-property "IODVDBundleName")
      (iokit-property "IOGLBundleName")
      (iokit-property "IOGVACodec")
      (iokit-property "IOGVAHEVCDecode")
      (iokit-property "IOGVAHEVCEncode")
      (iokit-property "IOPCITunnelled")
      (iokit-property "IOVARendererID")
      (iokit-property "MetalPluginName")
      (iokit-property "MetalPluginClassName")))

; depending on systems, the 1st, 2nd or both rules are necessary
  (allow user-preference-read (preference-domain "com.apple.HIToolbox"))
  (allow file-read-data (literal "/Library/Preferences/com.apple.HIToolbox.plist"))

  (allow user-preference-read (preference-domain "com.apple.ATS"))
  (allow file-read-data (literal "/Library/Preferences/.GlobalPreferences.plist"))

  (allow file-read*
      (subpath "/Library/ColorSync/Profiles")
      (subpath "/Library/Spelling")
      (literal "/")
      (literal "/private/tmp")
      (literal "/private/var/tmp")
      (home-literal "/.CFUserTextEncoding")
      (home-literal "/Library/Preferences/com.apple.DownloadAssessment.plist")
      (home-subpath "/Library/Colors")
      (home-subpath "/Library/ColorSync/Profiles")
      (home-subpath "/Library/Keyboard Layouts")
      (home-subpath "/Library/Input Methods")
      (home-subpath "/Library/Spelling"))

  (if (defined? 'file-map-executable)
    (begin
      (when testingReadPath1
        (allow file-read* file-map-executable (subpath testingReadPath1)))
      (when testingReadPath2
        (allow file-read* file-map-executable (subpath testingReadPath2)))
      (when testingReadPath3
        (allow file-read* file-map-executable (subpath testingReadPath3)))
      (when testingReadPath4
        (allow file-read* file-map-executable (subpath testingReadPath4))))
    (begin
      (when testingReadPath1
        (allow file-read* (subpath testingReadPath1)))
      (when testingReadPath2
        (allow file-read* (subpath testingReadPath2)))
      (when testingReadPath3
        (allow file-read* (subpath testingReadPath3)))
      (when testingReadPath4
        (allow file-read* (subpath testingReadPath4)))))

  (allow file-read-metadata (home-subpath "/Library"))

  (allow file-read-metadata (subpath "/private/var"))

  ; bug 1303987
  (if (string? debugWriteDir)
    (begin
      (allow file-write-data (subpath debugWriteDir))
      (allow file-write-create
        (require-all
          (subpath debugWriteDir)
          (vnode-type REGULAR-FILE)))))

  (allow-shared-list "org.mozilla.plugincontainer")

; Per-user and system-wide Extensions dir
  (allow file-read*
      (home-regex "/Library/Application Support/[^/]+/Extensions/")
      (regex "^/Library/Application Support/[^/]+/Extensions/"))

; bug 1393805
  (allow file-read*
      (home-subpath "/Library/Application Support/Mozilla/SystemExtensionsDev"))

; The following rules impose file access restrictions which get
; more restrictive in higher levels. When file-origin-specific
; content processes are used for file:// origin browsing, the
; global file-read* permission should be removed from each level.

; level 1: global read access permitted, no global write access
  (if (string=? sandbox-level-1 "TRUE") (allow file-read*))

; level 2: global read access permitted, no global write access,
;          no read/write access to ~/Library,
;          no read/write access to $PROFILE,
;          read access permitted to $PROFILE/{extensions,chrome}
  (if (string=? sandbox-level-2 "TRUE")
    (begin
      ; bug 1201935
      (allow file-read* (home-subpath "/Library/Caches/TemporaryItems"))
      (if (string=? hasProfileDir "TRUE")
        ; we have a profile dir
        (allow file-read* (require-all
          (require-not (home-subpath "/Library"))
          (require-not (subpath profileDir))))
        ; we don't have a profile dir
        (allow file-read* (require-not (home-subpath "/Library"))))))

  ; level 3: Does not have any of it's own rules. The global rules provide:
  ;          no global read/write access,
  ;          read access permitted to $PROFILE/{extensions,chrome}

  (if (string=? hasProfileDir "TRUE")
    ; we have a profile dir
    (allow file-read*
      (profile-subpath "/extensions")
      (profile-subpath "/chrome")))

; accelerated graphics
  (allow user-preference-read (preference-domain "com.apple.opengl"))
  (allow user-preference-read (preference-domain "com.nvidia.OpenGL"))
  (allow mach-lookup
      (global-name "com.apple.cvmsServ"))
  (if (>= macosMinorVersion 14)
    (allow mach-lookup
      (global-name "com.apple.MTLCompilerService")))
  (allow iokit-open
      (iokit-connection "IOAccelerator")
      (iokit-user-client-class "IOAccelerationUserClient")
      (iokit-user-client-class "IOSurfaceRootUserClient")
      (iokit-user-client-class "IOSurfaceSendRight")
      (iokit-user-client-class "IOFramebufferSharedUserClient")
      (iokit-user-client-class "AGPMClient")
      (iokit-user-client-class "AppleGraphicsControlClient"))

; bug 1153809
  (allow iokit-open
      (iokit-user-client-class "NVDVDContextTesla")
      (iokit-user-client-class "Gen6DVDContext"))

  ; Fonts
  (allow file-read*
    (subpath "/Library/Fonts")
    (subpath "/Library/Application Support/Apple/Fonts")
    (home-subpath "/Library/Fonts")
    ; Allow read access to paths allowed via sandbox extensions.
    ; This is needed for fonts in non-standard locations normally
    ; due to third party font managers. The extensions are
    ; automatically issued by the font server in response to font
    ; API calls.
    (extension "com.apple.app-sandbox.read"))
  ; Fonts may continue to work without explicitly allowing these
  ; services because, at present, connections are made to the services
  ; before the sandbox is enabled as a side-effect of some API calls.
  (allow mach-lookup
    (global-name "com.apple.fonts")
    (global-name "com.apple.FontObjectsServer"))
  (if (<= macosMinorVersion 11)
    (allow mach-lookup (global-name "com.apple.FontServer")))

  ; Fonts
  ; Workaround for sandbox extensions not being automatically
  ; issued for fonts on 10.11 and earlier versions (bug 1460917).
  (if (<= macosMinorVersion 11)
    (allow file-read*
      (regex #"\.[oO][tT][fF]$"          ; otf
             #"\.[tT][tT][fF]$"          ; ttf
             #"\.[tT][tT][cC]$"          ; ttc
             #"\.[oO][tT][cC]$"          ; otc
             #"\.[dD][fF][oO][nN][tT]$") ; dfont
      (home-subpath "/Library/FontCollections")
      (home-subpath "/Library/Application Support/Adobe/CoreSync/plugins/livetype")
      (home-subpath "/Library/Application Support/FontAgent")
      (home-subpath "/Library/Extensis/UTC") ; bug 1469657
      (subpath "/Library/Extensis/UTC")      ; bug 1469657
      (regex #"\.fontvault/")
      (home-subpath "/FontExplorer X/Font Library")))
)SANDBOX_LITERAL";

// These are additional rules that are added to the content process rules for
// file content processes.
static const char SandboxPolicyContentFileAddend[] = R"SANDBOX_LITERAL(
  ; This process has blanket file read privileges
  (allow file-read*)

  ; File content processes need access to iconservices to draw file icons in
  ; directory listings
  (allow mach-lookup (global-name "com.apple.iconservices"))
)SANDBOX_LITERAL";

// These are additional rules that are added to the content process rules when
// audio remoting is not enabled. (Once audio remoting is always used these
// will be deleted.)
static const char SandboxPolicyContentAudioAddend[] = R"SANDBOX_LITERAL(
  (allow ipc-posix-shm-read* ipc-posix-shm-write-data
    (ipc-posix-name-regex #"^AudioIO"))

  (allow mach-lookup
    (global-name "com.apple.audio.coreaudiod")
    (global-name "com.apple.audio.audiohald"))

  (if (>= macosMinorVersion 13)
    (allow mach-lookup
    ; bug 1376163
    (global-name "com.apple.audio.AudioComponentRegistrar")))

  (allow iokit-open (iokit-user-client-class "IOAudioEngineUserClient"))

  (allow file-read* (subpath "/Library/Audio/Plug-Ins"))

  (allow device-microphone)
)SANDBOX_LITERAL";

}  // namespace mozilla

#endif  // mozilla_SandboxPolicyContent_h

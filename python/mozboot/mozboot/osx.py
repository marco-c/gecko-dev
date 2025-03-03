# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals

import os
import re
import subprocess
import sys
import tempfile
try:
    from urllib2 import urlopen
except ImportError:
    from urllib.request import urlopen

from distutils.version import StrictVersion

from mozboot.base import BaseBootstrapper

HOMEBREW_BOOTSTRAP = 'https://raw.githubusercontent.com/Homebrew/install/master/install'
XCODE_APP_STORE = 'macappstore://itunes.apple.com/app/id497799835?mt=12'
XCODE_LEGACY = ('https://developer.apple.com/downloads/download.action?path=Developer_Tools/'
                'xcode_3.2.6_and_ios_sdk_4.3__final/xcode_3.2.6_and_ios_sdk_4.3.dmg')
JAVA_PATH = '/Library/Java/JavaVirtualMachines/adoptopenjdk-8.jdk/Contents/Home/bin'

MACPORTS_URL = {
    '14': 'https://distfiles.macports.org/MacPorts/MacPorts-2.5.4-10.14-Mojave.pkg',
    '13': 'https://distfiles.macports.org/MacPorts/MacPorts-2.5.4-10.13-HighSierra.pkg',
    '12': 'https://distfiles.macports.org/MacPorts/MacPorts-2.5.4-10.12-Sierra.pkg',
    '11': 'https://distfiles.macports.org/MacPorts/MacPorts-2.5.4-10.11-ElCapitan.pkg',
    '10': 'https://distfiles.macports.org/MacPorts/MacPorts-2.5.4-10.10-Yosemite.pkg',
    '9': 'https://distfiles.macports.org/MacPorts/MacPorts-2.5.4-10.9-Mavericks.pkg',
    '8': 'https://distfiles.macports.org/MacPorts/MacPorts-2.5.4-10.8-MountainLion.pkg',
    '7': 'https://distfiles.macports.org/MacPorts/MacPorts-2.5.4-10.7-Lion.pkg',
    '6': 'https://distfiles.macports.org/MacPorts/MacPorts-2.5.4-10.6-SnowLeopard.pkg', }

RE_CLANG_VERSION = re.compile('Apple (?:clang|LLVM) version (\d+\.\d+)')

APPLE_CLANG_MINIMUM_VERSION = StrictVersion('4.2')

XCODE_REQUIRED = '''
Xcode is required to build Firefox. Please complete the install of Xcode
through the App Store.

It's possible Xcode is already installed on this machine but it isn't being
detected. This is possible with developer preview releases of Xcode, for
example. To correct this problem, run:

  `xcode-select --switch /path/to/Xcode.app`.

e.g. `sudo xcode-select --switch /Applications/Xcode.app`.
'''

XCODE_REQUIRED_LEGACY = '''
You will need to download and install Xcode to build Firefox.

Please complete the Xcode download and then relaunch this script.
'''

XCODE_NO_DEVELOPER_DIRECTORY = '''
xcode-select says you don't have a developer directory configured. We think
this is due to you not having Xcode installed (properly). We're going to
attempt to install Xcode through the App Store. If the App Store thinks you
have Xcode installed, please run xcode-select by hand until it stops
complaining and then re-run this script.
'''

XCODE_COMMAND_LINE_TOOLS_MISSING = '''
The Xcode command line tools are required to build Firefox.
'''

INSTALL_XCODE_COMMAND_LINE_TOOLS_STEPS = '''
Perform the following steps to install the Xcode command line tools:

    1) Open Xcode.app
    2) Click through any first-run prompts
    3) From the main Xcode menu, select Preferences (Command ,)
    4) Go to the Download tab (near the right)
    5) Install the "Command Line Tools"

When that has finished installing, please relaunch this script.
'''

UPGRADE_XCODE_COMMAND_LINE_TOOLS = '''
An old version of the Xcode command line tools is installed. You will need to
install a newer version in order to compile Firefox. If Xcode itself is old,
its command line tools may be too old even if it claims there are no updates
available, so if you are seeing this message multiple times, please update
Xcode first.
'''

PACKAGE_MANAGER_INSTALL = '''
We will install the %s package manager to install required packages.

You will be prompted to install %s with its default settings. If you
would prefer to do this manually, hit CTRL+c, install %s yourself, ensure
"%s" is in your $PATH, and relaunch bootstrap.
'''

PACKAGE_MANAGER_PACKAGES = '''
We are now installing all required packages via %s. You will see a lot of
output as packages are built.
'''

PACKAGE_MANAGER_OLD_CLANG = '''
We require a newer compiler than what is provided by your version of Xcode.

We will install a modern version of Clang through %s.
'''

PACKAGE_MANAGER_CHOICE = '''
Please choose a package manager you'd like:
  1. Homebrew
  2. MacPorts (Does not yet support bootstrapping GeckoView/Firefox for Android.)
Your choice: '''

NO_PACKAGE_MANAGER_WARNING = '''
It seems you don't have any supported package manager installed.
'''

PACKAGE_MANAGER_EXISTS = '''
Looks like you have %s installed. We will install all required packages via %s.
'''

MULTI_PACKAGE_MANAGER_EXISTS = '''
It looks like you have multiple package managers installed.
'''

# May add support for other package manager on os x.
PACKAGE_MANAGER = {'Homebrew': 'brew',
                   'MacPorts': 'port'}

PACKAGE_MANAGER_CHOICES = ['Homebrew', 'MacPorts']

PACKAGE_MANAGER_BIN_MISSING = '''
A package manager is installed. However, your current shell does
not know where to find '%s' yet. You'll need to start a new shell
to pick up the environment changes so it can be found.

Please start a new shell or terminal window and run this
bootstrapper again.

If this problem persists, you will likely want to adjust your
shell's init script (e.g. ~/.bash_profile) to export a PATH
environment variable containing the location of your package
manager binary. e.g.

Homebrew:
    export PATH=/usr/local/bin:$PATH

MacPorts:
    export PATH=/opt/local/bin:$PATH
'''

BAD_PATH_ORDER = '''
Your environment's PATH variable lists a system path directory (%s)
before the path to your package manager's binaries (%s).
This means that the package manager's binaries likely won't be
detected properly.

Modify your shell's configuration (e.g. ~/.profile or
~/.bash_profile) to have %s appear in $PATH before %s. e.g.

    export PATH=%s:$PATH

Once this is done, start a new shell (likely Command+T) and run
this bootstrap again.
'''


class OSXBootstrapper(BaseBootstrapper):
    def __init__(self, version, **kwargs):
        BaseBootstrapper.__init__(self, **kwargs)

        self.os_version = StrictVersion(version)

        if self.os_version < StrictVersion('10.6'):
            raise Exception('OS X 10.6 or above is required.')

        self.minor_version = version.split('.')[1]

    def install_system_packages(self):
        self.ensure_xcode()

        choice = self.ensure_package_manager()
        self.package_manager = choice
        getattr(self, 'ensure_%s_system_packages' % self.package_manager)()

    def install_browser_packages(self):
        getattr(self, 'ensure_%s_browser_packages' % self.package_manager)()

    def install_browser_artifact_mode_packages(self):
        getattr(self, 'ensure_%s_browser_packages' % self.package_manager)(artifact_mode=True)

    def install_mobile_android_packages(self):
        getattr(self, 'ensure_%s_mobile_android_packages' % self.package_manager)()

    def install_mobile_android_artifact_mode_packages(self):
        getattr(self, 'ensure_%s_mobile_android_packages' %
                self.package_manager)(artifact_mode=True)

    def suggest_mobile_android_mozconfig(self):
        getattr(self, 'suggest_%s_mobile_android_mozconfig' % self.package_manager)()

    def suggest_mobile_android_artifact_mode_mozconfig(self):
        getattr(self, 'suggest_%s_mobile_android_mozconfig' %
                self.package_manager)(artifact_mode=True)

    def ensure_xcode(self):
        if self.os_version < StrictVersion('10.7'):
            if not os.path.exists('/Developer/Applications/Xcode.app'):
                print(XCODE_REQUIRED_LEGACY)

                subprocess.check_call(['open', XCODE_LEGACY])
                sys.exit(1)

        # OS X 10.7 have Xcode come from the app store. However, users can
        # still install Xcode into any arbitrary location. We honor the
        # location of Xcode as set by xcode-select. This should also pick up
        # developer preview releases of Xcode, which can be installed into
        # paths like /Applications/Xcode5-DP6.app.
        elif self.os_version >= StrictVersion('10.7'):
            select = self.which('xcode-select')
            try:
                output = self.check_output([select, '--print-path'],
                                           stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError as e:
                # This seems to appear on fresh OS X machines before any Xcode
                # has been installed. It may only occur on OS X 10.9 and later.
                if b'unable to get active developer directory' in e.output:
                    print(XCODE_NO_DEVELOPER_DIRECTORY)
                    self._install_xcode_app_store()
                    assert False  # Above should exit.

                output = e.output

            # This isn't the most robust check in the world. It relies on the
            # default value not being in an application bundle, which seems to
            # hold on at least Mavericks.
            if b'.app/' not in output:
                print(XCODE_REQUIRED)
                self._install_xcode_app_store()
                assert False  # Above should exit.

        # Once Xcode is installed, you need to agree to the license before you can
        # use it.
        try:
            output = self.check_output(['/usr/bin/xcrun', 'clang'],
                                       stderr=subprocess.STDOUT)
        except subprocess.CalledProcessError as e:
            if b'license' in e.output:
                xcodebuild = self.which('xcodebuild')
                try:
                    self.check_output([xcodebuild, '-license'],
                                      stderr=subprocess.STDOUT)
                except subprocess.CalledProcessError as e:
                    if b'requires admin privileges' in e.output:
                        self.run_as_root([xcodebuild, '-license'])

        # Even then we're not done! We need to install the Xcode command line tools.
        # As of Mountain Lion, apparently the only way to do this is to go through a
        # menu dialog inside Xcode itself. We're not making this up.
        if self.os_version >= StrictVersion('10.7'):
            if not os.path.exists('/usr/bin/clang'):
                print(XCODE_COMMAND_LINE_TOOLS_MISSING)
                print(INSTALL_XCODE_COMMAND_LINE_TOOLS_STEPS)
                sys.exit(1)

            output = self.check_output(['/usr/bin/clang', '--version'])
            match = RE_CLANG_VERSION.search(output)
            if match is None:
                raise Exception('Could not determine Clang version.')

            version = StrictVersion(match.group(1))

            if version < APPLE_CLANG_MINIMUM_VERSION:
                print(UPGRADE_XCODE_COMMAND_LINE_TOOLS)
                print(INSTALL_XCODE_COMMAND_LINE_TOOLS_STEPS)
                sys.exit(1)

    def _install_xcode_app_store(self):
        subprocess.check_call(['open', XCODE_APP_STORE])
        print('Once the install has finished, please relaunch this script.')
        sys.exit(1)

    def _ensure_homebrew_found(self):
        if not hasattr(self, 'brew'):
            self.brew = self.which('brew')
        # Earlier code that checks for valid package managers ensures
        # which('brew') is found.
        assert self.brew is not None

    def _ensure_homebrew_packages(self, packages, extra_brew_args=[]):
        self._ensure_homebrew_found()
        cmd = [self.brew] + extra_brew_args

        installed = self.check_output(cmd + ['list']).split()

        printed = False

        for package in packages:
            if package in installed:
                continue

            if not printed:
                print(PACKAGE_MANAGER_PACKAGES % ('Homebrew',))
                printed = True

            subprocess.check_call(cmd + ['install', package])

        return printed

    def _ensure_homebrew_casks(self, casks):
        self._ensure_homebrew_found()

        # Ensure that we can access old versions of packages.  This is
        # idempotent, so no need to avoid repeat invocation.
        self.check_output([self.brew, 'tap', 'homebrew/cask-versions'])

        # Change |brew install cask| into |brew cask install cask|.
        return self._ensure_homebrew_packages(casks, extra_brew_args=['cask'])

    def ensure_homebrew_system_packages(self):
        # We need to install Python because Mercurial requires the
        # Python development headers which are missing from OS X (at
        # least on 10.8) and because the build system wants a version
        # newer than what Apple ships.
        packages = [
            'autoconf@2.13',
            'git',
            'gnu-tar',
            'mercurial',
            'node',
            'python',
            'python@2',
            'terminal-notifier',
            'watchman',
        ]
        self._ensure_homebrew_packages(packages)

    def ensure_homebrew_browser_packages(self, artifact_mode=False):
        # TODO: Figure out what not to install for artifact mode
        packages = [
            'nasm',
            'yasm',
        ]
        self._ensure_homebrew_packages(packages)

    def ensure_homebrew_mobile_android_packages(self, artifact_mode=False):
        # Multi-part process:
        # 1. System packages.
        # 2. Android SDK. Android NDK only if we are not in artifact mode. Android packages.

        # 1. System packages.
        packages = [
            'wget',
        ]
        self._ensure_homebrew_packages(packages)

        casks = [
            'adoptopenjdk8',
        ]
        self._ensure_homebrew_casks(casks)

        is_64bits = sys.maxsize > 2**32
        if not is_64bits:
            raise Exception('You need a 64-bit version of Mac OS X to build '
                            'GeckoView/Firefox for Android.')

        # 2. Android pieces.
        # Prefer homebrew's java binary by putting it on the path first.
        os.environ['PATH'] = \
            '{}{}{}'.format(JAVA_PATH, os.pathsep, os.environ['PATH'])
        self.ensure_java()
        from mozboot import android

        android.ensure_android('macosx', artifact_mode=artifact_mode,
                               no_interactive=self.no_interactive)

    def suggest_homebrew_mobile_android_mozconfig(self, artifact_mode=False):
        from mozboot import android
        android.suggest_mozconfig('macosx', artifact_mode=artifact_mode)

    def _ensure_macports_packages(self, packages):
        self.port = self.which('port')
        assert self.port is not None

        installed = set(self.check_output([self.port, 'installed']).split())

        missing = [package for package in packages if package not in installed]
        if missing:
            print(PACKAGE_MANAGER_PACKAGES % ('MacPorts',))
            self.run_as_root([self.port, '-v', 'install'] + missing)

    def ensure_macports_system_packages(self):
        packages = [
            'python27',
            'python36',
            'py27-gnureadline',
            'mercurial',
            'autoconf213',
            'gnutar',
            'watchman',
            'nodejs8'
        ]

        self._ensure_macports_packages(packages)

        pythons = set(self.check_output([self.port, 'select', '--list', 'python']).split('\n'))
        active = ''
        for python in pythons:
            if 'active' in python:
                active = python
        if 'python27' not in active:
            self.run_as_root([self.port, 'select', '--set', 'python', 'python27'])
        else:
            print('The right python version is already active.')

    def ensure_macports_browser_packages(self, artifact_mode=False):
        # TODO: Figure out what not to install for artifact mode
        packages = [
            'nasm',
            'yasm',
        ]

        self._ensure_macports_packages(packages)

    def ensure_macports_mobile_android_packages(self, artifact_mode=False):
        # Multi-part process:
        # 1. System packages.
        # 2. Android SDK. Android NDK only if we are not in artifact mode. Android packages.

        # 1. System packages.
        packages = [
            'wget',
        ]
        self._ensure_macports_packages(packages)

        is_64bits = sys.maxsize > 2**32
        if not is_64bits:
            raise Exception('You need a 64-bit version of Mac OS X to build '
                            'GeckoView/Firefox for Android.')

        # 2. Android pieces.
        self.ensure_java()
        from mozboot import android
        android.ensure_android('macosx', artifact_mode=artifact_mode,
                               no_interactive=self.no_interactive)

    def suggest_macports_mobile_android_mozconfig(self, artifact_mode=False):
        from mozboot import android
        android.suggest_mozconfig('macosx', artifact_mode=artifact_mode)

    def ensure_package_manager(self):
        '''
        Search package mgr in sys.path, if none is found, prompt the user to install one.
        If only one is found, use that one. If both are found, prompt the user to choose
        one.
        '''
        installed = []
        for name, cmd in PACKAGE_MANAGER.iteritems():
            if self.which(cmd) is not None:
                installed.append(name)

        active_name, active_cmd = None, None

        if not installed:
            print(NO_PACKAGE_MANAGER_WARNING)
            choice = self.prompt_int(prompt=PACKAGE_MANAGER_CHOICE, low=1, high=2)
            active_name = PACKAGE_MANAGER_CHOICES[choice - 1]
            active_cmd = PACKAGE_MANAGER[active_name]
            getattr(self, 'install_%s' % active_name.lower())()
        elif len(installed) == 1:
            print(PACKAGE_MANAGER_EXISTS % (installed[0], installed[0]))
            active_name = installed[0]
            active_cmd = PACKAGE_MANAGER[active_name]
        else:
            print(MULTI_PACKAGE_MANAGER_EXISTS)
            choice = self.prompt_int(prompt=PACKAGE_MANAGER_CHOICE, low=1, high=2)

            active_name = PACKAGE_MANAGER_CHOICES[choice - 1]
            active_cmd = PACKAGE_MANAGER[active_name]

        # Ensure the active package manager is in $PATH and it comes before
        # /usr/bin. If it doesn't come before /usr/bin, we'll pick up system
        # packages before package manager installed packages and the build may
        # break.
        p = self.which(active_cmd)
        if not p:
            print(PACKAGE_MANAGER_BIN_MISSING % active_cmd)
            sys.exit(1)

        p_dir = os.path.dirname(p)
        for path in os.environ['PATH'].split(os.pathsep):
            if path == p_dir:
                break

            for check in ('/bin', '/usr/bin'):
                if path == check:
                    print(BAD_PATH_ORDER % (check, p_dir, p_dir, check, p_dir))
                    sys.exit(1)

        return active_name.lower()

    def ensure_clang_static_analysis_package(self, state_dir, checkout_root):
        from mozboot import static_analysis
        self.install_toolchain_static_analysis(
            state_dir, checkout_root, static_analysis.MACOS_CLANG_TIDY)

    def ensure_sccache_packages(self, state_dir, checkout_root):
        from mozboot import sccache

        self.install_toolchain_artifact(state_dir, checkout_root, sccache.MACOS_SCCACHE)
        self.install_toolchain_artifact(state_dir, checkout_root,
                                        sccache.RUSTC_DIST_TOOLCHAIN,
                                        no_unpack=True)
        self.install_toolchain_artifact(state_dir, checkout_root,
                                        sccache.CLANG_DIST_TOOLCHAIN,
                                        no_unpack=True)

    def ensure_stylo_packages(self, state_dir, checkout_root):
        from mozboot import stylo
        self.install_toolchain_artifact(state_dir, checkout_root, stylo.MACOS_CLANG)
        self.install_toolchain_artifact(state_dir, checkout_root, stylo.MACOS_CBINDGEN)

    def ensure_nasm_packages(self, state_dir, checkout_root):
        # installed via ensure_browser_packages
        pass

    def ensure_node_packages(self, state_dir, checkout_root):
        # XXX from necessary?
        from mozboot import node
        self.install_toolchain_artifact(state_dir, checkout_root, node.OSX)

    def install_homebrew(self):
        print(PACKAGE_MANAGER_INSTALL % ('Homebrew', 'Homebrew', 'Homebrew', 'brew'))
        bootstrap = urlopen(url=HOMEBREW_BOOTSTRAP, timeout=20).read()
        with tempfile.NamedTemporaryFile() as tf:
            tf.write(bootstrap)
            tf.flush()

            subprocess.check_call(['ruby', tf.name])

    def install_macports(self):
        url = MACPORTS_URL.get(self.minor_version, None)
        if not url:
            raise Exception('We do not have a MacPorts install URL for your '
                            'OS X version. You will need to install MacPorts manually.')

        print(PACKAGE_MANAGER_INSTALL % ('MacPorts', 'MacPorts', 'MacPorts', 'port'))
        pkg = urlopen(url=url, timeout=300).read()
        with tempfile.NamedTemporaryFile(suffix='.pkg') as tf:
            tf.write(pkg)
            tf.flush()

            self.run_as_root(['installer', '-pkg', tf.name, '-target', '/'])

    def _update_package_manager(self):
        if self.package_manager == 'homebrew':
            subprocess.check_call([self.brew, '-v', 'update'])
        else:
            assert self.package_manager == 'macports'
            self.run_as_root([self.port, 'selfupdate'])

    def _upgrade_package(self, package):
        self._ensure_package_manager_updated()

        if self.package_manager == 'homebrew':
            try:
                subprocess.check_output([self.brew, '-v', 'upgrade', package],
                                        stderr=subprocess.STDOUT)
            except subprocess.CalledProcessError as e:
                if b'already installed' not in e.output:
                    raise
        else:
            assert self.package_manager == 'macports'

            self.run_as_root([self.port, 'upgrade', package])

    def upgrade_mercurial(self, current):
        self._upgrade_package('mercurial')

    def upgrade_python(self, current):
        if self.package_manager == 'homebrew':
            self._upgrade_package('python')
        else:
            self._upgrade_package('python27')

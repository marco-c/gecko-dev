# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, # You can obtain one at http://mozilla.org/MPL/2.0/.

from __future__ import absolute_import, print_function, unicode_literals

import errno
import hashlib
import logging
import os
import re
import subprocess
import sys
from distutils.version import LooseVersion

import mozpack.path as mozpath
from mozbuild.base import (
    BuildEnvironmentNotFoundException,
    MozbuildObject,
)


class VendorRust(MozbuildObject):
    def get_cargo_path(self):
        try:
            return self.substs['CARGO']
        except (BuildEnvironmentNotFoundException, KeyError):
            # Default if this tree isn't configured.
            from mozfile import which
            cargo = which('cargo')
            if not cargo:
                raise OSError(errno.ENOENT, "Could not find 'cargo' on your $PATH.")
            return cargo

    def check_cargo_version(self, cargo):
        '''
        Ensure that cargo is new enough. cargo 0.12 added support
        for source replacement, which is required for vendoring to work.
        '''
        out = subprocess.check_output([cargo, '--version']).splitlines()[0]
        if not out.startswith('cargo'):
            return False
        return LooseVersion(out.split()[1]) >= b'0.13'

    def check_cargo_vendor_version(self, cargo):
        '''
        Ensure that cargo-vendor is new enough. cargo-vendor 0.1.13 and newer
        strips out .cargo-ok, .orig and .rej files, and deals with [patch]
        replacements in Cargo.toml files which we want.  Version 0.1.14 and up
        handles local modifications to vendored crates (bug 1323557).
        '''
        for l in subprocess.check_output([cargo, 'install', '--list']).splitlines():
            # The line looks like one of the following:
            #  cargo-vendor v0.1.12:
            #  cargo-vendor v0.1.12 (file:///path/to/local/build/cargo-vendor):
            # and we want to extract the version part of it
            m = re.match('cargo-vendor v((\d+\.)*\d+)', l)
            if m:
                version = m.group(1)
                return LooseVersion(version) >= b'0.1.23'
        return False

    def check_modified_files(self):
        '''
        Ensure that there aren't any uncommitted changes to files
        in the working copy, since we're going to change some state
        on the user. Allow changes to Cargo.{toml,lock} since that's
        likely to be a common use case.
        '''
        modified = [f for f in self.repository.get_changed_files(
            'M') if os.path.basename(f) not in ('Cargo.toml', 'Cargo.lock')]
        if modified:
            self.log(logging.ERROR, 'modified_files', {},
                     '''You have uncommitted changes to the following files:

{files}

Please commit or stash these changes before vendoring, or re-run with `--ignore-modified`.
'''.format(files='\n'.join(sorted(modified))))
            sys.exit(1)

    def check_openssl(self):
        '''
        Set environment flags for building with openssl.

        MacOS doesn't include openssl, but the openssl-sys crate used by
        mach-vendor expects one of the system. It's common to have one
        installed in /usr/local/opt/openssl by homebrew, but custom link
        flags are necessary to build against it.
        '''

        test_paths = ['/usr/include', '/usr/local/include']
        if any([os.path.exists(os.path.join(path, 'openssl/ssl.h')) for path in test_paths]):
            # Assume we can use one of these system headers.
            return None

        if os.path.exists('/usr/local/opt/openssl/include/openssl/ssl.h'):
            # Found a likely homebrew install.
            self.log(logging.INFO, 'openssl', {},
                     'Using OpenSSL in /usr/local/opt/openssl')
            return {
                'OPENSSL_INCLUDE_DIR': '/usr/local/opt/openssl/include',
                'OPENSSL_LIB_DIR': '/usr/local/opt/openssl/lib',
            }

        self.log(logging.ERROR, 'openssl', {}, "OpenSSL not found!")
        return None

    def _ensure_cargo(self):
        '''
        Ensures all the necessary cargo bits are installed.

        Returns the path to cargo if successful, None otherwise.
        '''
        cargo = self.get_cargo_path()
        if not self.check_cargo_version(cargo):
            self.log(logging.ERROR, 'cargo_version', {},
                     'Cargo >= 0.13 required (install Rust 1.12 or newer)')
            return None
        else:
            self.log(logging.DEBUG, 'cargo_version', {}, 'cargo is new enough')
        have_vendor = any(l.strip() == 'vendor' for l in subprocess.check_output(
            [cargo, '--list']).splitlines())
        if not have_vendor:
            self.log(logging.INFO, 'installing', {},
                     'Installing cargo-vendor (this may take a few minutes)...')
            env = self.check_openssl()
            self.run_process(args=[cargo, 'install', 'cargo-vendor'],
                             append_env=env)
        elif not self.check_cargo_vendor_version(cargo):
            self.log(
                logging.INFO, 'cargo_vendor', {},
                ('cargo-vendor >= 0.1.23 required; '
                 'force-reinstalling (this may take a few minutes)...')
                )
            env = self.check_openssl()
            self.run_process(args=[cargo, 'install', '--force', 'cargo-vendor'],
                             append_env=env)
        else:
            self.log(logging.DEBUG, 'cargo_vendor', {},
                     'sufficiently new cargo-vendor is already installed')

        return cargo

    # A whitelist of acceptable license identifiers for the
    # packages.license field from https://spdx.org/licenses/.  Cargo
    # documentation claims that values are checked against the above
    # list and that multiple entries can be separated by '/'.  We
    # choose to list all combinations instead for the sake of
    # completeness and because some entries below obviously do not
    # conform to the format prescribed in the documentation.
    #
    # It is insufficient to have additions to this whitelist reviewed
    # solely by a build peer; any additions must be checked by somebody
    # competent to review licensing minutiae.

    # Licenses for code used at runtime. Please see the above comment before
    # adding anything to this list.
    RUNTIME_LICENSE_WHITELIST = [
        'Apache-2.0',
        'Apache-2.0 WITH LLVM-exception',
        # BSD-2-Clause and BSD-3-Clause are ok, but packages using them
        # must be added to the appropriate section of about:licenses.
        # To encourage people to remember to do that, we do not whitelist
        # the licenses themselves, and we require the packages to be added
        # to RUNTIME_LICENSE_PACKAGE_WHITELIST below.
        'CC0-1.0',
        'ISC',
        'MIT',
        'MPL-2.0',
        'Unlicense',
    ]

    # Licenses for code used at build time (e.g. code generators). Please see the above
    # comments before adding anything to this list.
    BUILDTIME_LICENSE_WHITELIST = {
        'BSD-3-Clause': [
            'adler32',
            'bindgen',
            'fuchsia-zircon',
            'fuchsia-zircon-sys',
            'fuchsia-cprng',
        ]
    }

    # This whitelist should only be used for packages that use an acceptable
    # license, but that also need to explicitly mentioned in about:license.
    RUNTIME_LICENSE_PACKAGE_WHITELIST = {
        'BSD-2-Clause': [
            'arrayref',
            'cloudabi',
            'Inflector',
            'mach',
        ],
        'BSD-3-Clause': [
        ]
    }

    # This whitelist should only be used for packages that use a
    # license-file and for which the license-file entry has been
    # reviewed.  The table is keyed by package names and maps to the
    # sha256 hash of the license file that we reviewed.
    #
    # As above, it is insufficient to have additions to this whitelist
    # reviewed solely by a build peer; any additions must be checked by
    # somebody competent to review licensing minutiae.
    RUNTIME_LICENSE_FILE_PACKAGE_WHITELIST = {
        # MIT
        'deque': '6485b8ed310d3f0340bf1ad1f47645069ce4069dcc6bb46c7d5c6faf41de1fdb',
        # we're whitelisting this fuchsia crate because it doesn't get built in the final
        # product but has a license-file that needs ignoring
        'fuchsia-cprng': '03b114f53e6587a398931762ee11e2395bfdba252a329940e2c8c9e81813845b',
    }

    @staticmethod
    def runtime_license(package, license_string):
        """Cargo docs say:
        ---
        https://doc.rust-lang.org/cargo/reference/manifest.html

        This is an SPDX 2.1 license expression for this package.  Currently
        crates.io will validate the license provided against a whitelist of
        known license and exception identifiers from the SPDX license list
        2.4.  Parentheses are not currently supported.

        Multiple licenses can be separated with a `/`, although that usage
        is deprecated.  Instead, use a license expression with AND and OR
        operators to get more explicit semantics.
        ---
        But I have no idea how you can meaningfully AND licenses, so
        we will abort if that is detected. We'll handle `/` and OR as
        equivalent and approve is any is in our approved list."""

        if re.search(r'\s+AND', license_string):
            return False

        license_list = re.split(r'\s*/\s*|\s+OR\s+', license_string)
        for license in license_list:
            if license in VendorRust.RUNTIME_LICENSE_WHITELIST:
                return True
            if package in VendorRust.RUNTIME_LICENSE_PACKAGE_WHITELIST.get(license, []):
                return True
        return False

    def _check_licenses(self, vendor_dir):
        LICENSE_LINE_RE = re.compile(r'\s*license\s*=\s*"([^"]+)"')
        LICENSE_FILE_LINE_RE = re.compile(r'\s*license[-_]file\s*=\s*"([^"]+)"')

        def verify_acceptable_license(package, license):
            self.log(logging.DEBUG, 'package_license', {},
                     'has license {}'.format(license))

            if not self.runtime_license(package, license):
                if license not in self.BUILDTIME_LICENSE_WHITELIST:
                    self.log(logging.ERROR, 'package_license_error', {},
                             '''Package {} has a non-approved license: {}.

    Please request license review on the package's license.  If the package's license
    is approved, please add it to the whitelist of suitable licenses.
    '''.format(package, license))
                    return False
                elif package not in self.BUILDTIME_LICENSE_WHITELIST[license]:
                    self.log(logging.ERROR, 'package_license_error', {},
                             '''Package {} has a license that is approved for build-time dependencies: {}
    but the package itself is not whitelisted as being a build-time only package.

    If your package is build-time only, please add it to the whitelist of build-time
    only packages. Otherwise, you need to request license review on the package's license.
    If the package's license is approved, please add it to the whitelist of suitable licenses.
    '''.format(package, license))
                    return False

        def check_package(package):
            self.log(logging.DEBUG, 'package_check', {},
                     'Checking license for {}'.format(package))

            toml_file = os.path.join(vendor_dir, package, 'Cargo.toml')

            # pytoml is not sophisticated enough to parse Cargo.toml files
            # with [target.'cfg(...)'.dependencies sections, so we resort
            # to scanning individual lines.
            with open(toml_file, 'r') as f:
                license_lines = [l for l in f if l.strip().startswith(b'license')]
                license_matches = list(
                    filter(lambda x: x, [LICENSE_LINE_RE.match(l) for l in license_lines]))
                license_file_matches = list(
                    filter(lambda x: x, [LICENSE_FILE_LINE_RE.match(l) for l in license_lines]))

                # License information is optional for crates to provide, but
                # we require it.
                if not license_matches and not license_file_matches:
                    self.log(logging.ERROR, 'package_no_license', {},
                             'package {} does not provide a license'.format(package))
                    return False

                # The Cargo.toml spec suggests that crates should either have
                # `license` or `license-file`, but not both.  We might as well
                # be defensive about that, though.
                if len(license_matches) > 1 or len(license_file_matches) > 1 or \
                   license_matches and license_file_matches:
                    self.log(logging.ERROR, 'package_many_licenses', {},
                             'package {} provides too many licenses'.format(package))
                    return False

                if license_matches:
                    license = license_matches[0].group(1)
                    verify_acceptable_license(package, license)
                else:
                    license_file = license_file_matches[0].group(1)
                    self.log(logging.DEBUG, 'package_license_file', {},
                             'has license-file {}'.format(license_file))

                    if package not in self.RUNTIME_LICENSE_FILE_PACKAGE_WHITELIST:
                        self.log(logging.ERROR, 'package_license_file_unknown', {},
                                 '''Package {} has an unreviewed license file: {}.

Please request review on the provided license; if approved, the package can be added
to the whitelist of packages whose licenses are suitable.
'''.format(package, license_file))
                        return False

                    approved_hash = self.RUNTIME_LICENSE_FILE_PACKAGE_WHITELIST[package]
                    license_contents = open(os.path.join(
                        vendor_dir, package, license_file), 'r').read()
                    current_hash = hashlib.sha256(license_contents).hexdigest()
                    if current_hash != approved_hash:
                        self.log(logging.ERROR, 'package_license_file_mismatch', {},
                                 '''Package {} has changed its license file: {} (hash {}).

Please request review on the provided license; if approved, please update the
license file's hash.
'''.format(package, license_file, current_hash))
                        return False

                return True

        # Force all of the packages to be checked for license information
        # before reducing via `all`, so all license issues are found in a
        # single `mach vendor rust` invocation.
        results = [check_package(p) for p in os.listdir(vendor_dir)
                   if os.path.isdir(os.path.join(vendor_dir, p))]
        return all(results)

    def vendor(self, ignore_modified=False,
               build_peers_said_large_imports_were_ok=False):
        self.populate_logger()
        self.log_manager.enable_unstructured()
        if not ignore_modified:
            self.check_modified_files()

        cargo = self._ensure_cargo()
        if not cargo:
            return

        relative_vendor_dir = 'third_party/rust'
        vendor_dir = mozpath.join(self.topsrcdir, relative_vendor_dir)

        # We use check_call instead of mozprocess to ensure errors are displayed.
        # We do an |update -p| here to regenerate the Cargo.lock file with minimal
        # changes. See bug 1324462
        subprocess.check_call([cargo, 'update', '-p', 'gkrust'], cwd=self.topsrcdir)

        subprocess.check_call([cargo, 'vendor', '--quiet', '--sync',
                               'Cargo.lock'] + [vendor_dir], cwd=self.topsrcdir)

        if not self._check_licenses(vendor_dir):
            self.log(
                logging.ERROR, 'license_check_failed', {},
                '''The changes from `mach vendor rust` will NOT be added to version control.''')
            sys.exit(1)

        self.repository.add_remove_files(vendor_dir)

        # 100k is a reasonable upper bound on source file size.
        FILESIZE_LIMIT = 100 * 1024
        large_files = set()
        cumulative_added_size = 0
        for f in self.repository.get_changed_files('A'):
            path = mozpath.join(self.topsrcdir, f)
            size = os.stat(path).st_size
            cumulative_added_size += size
            if size > FILESIZE_LIMIT:
                large_files.add(f)

        # Forcefully complain about large files being added, as history has
        # shown that large-ish files typically are not needed.
        if large_files and not build_peers_said_large_imports_were_ok:
            self.log(logging.ERROR, 'filesize_check', {},
                     '''The following files exceed the filesize limit of {size}:

{files}

Please find a way to reduce the sizes of these files or talk to a build
peer about the particular large files you are adding.

The changes from `mach vendor rust` will NOT be added to version control.
'''.format(files='\n'.join(sorted(large_files)), size=FILESIZE_LIMIT))
            self.repository.forget_add_remove_files(vendor_dir)
            sys.exit(1)

        # Only warn for large imports, since we may just have large code
        # drops from time to time (e.g. importing features into m-c).
        SIZE_WARN_THRESHOLD = 5 * 1024 * 1024
        if cumulative_added_size >= SIZE_WARN_THRESHOLD:
            self.log(logging.WARN, 'filesize_check', {},
                     '''Your changes add {size} bytes of added files.

Please consider finding ways to reduce the size of the vendored packages.
For instance, check the vendored packages for unusually large test or
benchmark files that don't need to be published to crates.io and submit
a pull request upstream to ignore those files when publishing.'''.format(
                size=cumulative_added_size)
            )

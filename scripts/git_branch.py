"""
PlatformIO pre-build script: inject a traceable CROSSPOINT_VERSION for
development environments.

Results in a version string like:
2026.05.31.1-m5paper+master.771fbaf
2026.05.31.1-m5paper+master.771fbaf.dirty

Release environments are unaffected; they set CROSSPOINT_VERSION in the ini.
"""

import configparser
import os
import subprocess
import sys


def warn(msg):
    print(f'WARNING [git_branch.py]: {msg}', file=sys.stderr)


def run_git(project_dir, *args):
    return subprocess.check_output(
        ['git', *args],
        text=True, stderr=subprocess.PIPE, cwd=project_dir
    ).strip()


def clean_version_part(value, fallback='unknown'):
    cleaned = []
    for char in value:
        if char.isalnum() or char in '._-':
            cleaned.append(char)
        else:
            cleaned.append('-')
    result = ''.join(cleaned).strip('.-_')
    return result or fallback


def get_git_branch(project_dir):
    try:
        branch = run_git(project_dir, 'rev-parse', '--abbrev-ref', 'HEAD')
        # Detached HEAD — show the short SHA instead
        if branch == 'HEAD':
            branch = run_git(project_dir, 'rev-parse', '--short', 'HEAD')
        return clean_version_part(branch)
    except FileNotFoundError:
        warn('git not found on PATH; branch suffix will be "unknown"')
        return 'unknown'
    except subprocess.CalledProcessError as e:
        warn(f'git command failed (exit {e.returncode}): {e.stderr.strip()}; branch suffix will be "unknown"')
        return 'unknown'
    except Exception as e:
        warn(f'Unexpected error reading git branch: {e}; branch suffix will be "unknown"')
        return 'unknown'


def get_git_sha(project_dir):
    try:
        return clean_version_part(run_git(project_dir, 'rev-parse', '--short=7', 'HEAD'))
    except FileNotFoundError:
        warn('git not found on PATH; sha suffix will be "unknown"')
        return 'unknown'
    except subprocess.CalledProcessError as e:
        warn(f'git command failed (exit {e.returncode}): {e.stderr.strip()}; sha suffix will be "unknown"')
        return 'unknown'
    except Exception as e:
        warn(f'Unexpected error reading git sha: {e}; sha suffix will be "unknown"')
        return 'unknown'


def get_dirty_suffix(project_dir):
    try:
        status = run_git(project_dir, 'status', '--porcelain')
        return '.dirty' if status else ''
    except FileNotFoundError:
        warn('git not found on PATH; dirty marker will be omitted')
        return ''
    except subprocess.CalledProcessError as e:
        warn(f'git command failed (exit {e.returncode}): {e.stderr.strip()}; dirty marker will be omitted')
        return ''
    except Exception as e:
        warn(f'Unexpected error reading git status: {e}; dirty marker will be omitted')
        return ''


def get_base_version(project_dir):
    ini_path = os.path.join(project_dir, 'platformio.ini')
    if not os.path.isfile(ini_path):
        warn(f'platformio.ini not found at {ini_path}; base version will be "0.0.0"')
        return '0.0.0'
    config = configparser.ConfigParser()
    config.read(ini_path)
    if not config.has_option('biscuit', 'version'):
        warn('No [biscuit] version in platformio.ini; base version will be "0.0.0"')
        return '0.0.0'
    return clean_version_part(config.get('biscuit', 'version'), fallback='0.0.0')


def inject_version(env):
    # Only applies to development environments; release envs set the version
    # via build_flags in platformio.ini and are unaffected.
    env_name = env['PIOENV']
    dev_suffixes = {
        'default': 'default',
        'm5paper': 'm5paper',
    }
    if env_name not in dev_suffixes:
        return

    project_dir = env['PROJECT_DIR']
    base_version = get_base_version(project_dir)
    branch = get_git_branch(project_dir)
    sha = get_git_sha(project_dir)
    dirty = get_dirty_suffix(project_dir)
    version_string = f'{base_version}-{dev_suffixes[env_name]}+{branch}.{sha}{dirty}'

    env.Append(CPPDEFINES=[('CROSSPOINT_VERSION', f'\\"{version_string}\\"')])
    print(f'biscuit. build version: {version_string}')


# PlatformIO/SCons entry point — Import and env are SCons builtins injected at runtime.
# When run directly with Python (e.g. for validation), a lightweight fake env is used
# so the git/version logic can be exercised without a full build.
try:
    Import('env')           # noqa: F821  # type: ignore[name-defined]
    inject_version(env)     # noqa: F821  # type: ignore[name-defined]
except NameError:
    class _Env(dict):
        def Append(self, **_): pass

    _project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    inject_version(_Env({'PIOENV': 'default', 'PROJECT_DIR': _project_dir}))

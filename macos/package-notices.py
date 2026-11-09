"""Include license notices and the dependency source inventory in the app."""
from pathlib import Path
import shutil
import subprocess
import sys

bundle = Path(sys.argv[1])
qt = Path(sys.argv[2])
destination = bundle / 'Contents/Resources/licenses'
destination.mkdir(parents=True, exist_ok=True)
shutil.copy2('LICENSE', destination / 'Memento-COPYING')
for label, root in [('MacPorts', Path('/opt/local/share/doc')), ('Qt', qt / 'Licenses')]:
    if root.exists():
        for path in root.rglob('*'):
            if path.is_file() and (label == 'Qt' or any(
                    word in path.name.lower() for word in ['license', 'copying', 'copyright', 'notice'])):
                target = destination / label / path.relative_to(root)
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(path, target)
(destination / 'MacPorts-packages.txt').write_text(subprocess.check_output(
    ['/opt/local/bin/port', 'installed'], text=True))
# Preserve the exact shipped helper's license declaration and build options
# alongside the full dependency license files and pinned source inventory.
ffmpeg = bundle / 'Contents/MacOS/ffmpeg'
for option, name in [('-L', 'FFmpeg-license.txt'), ('-version', 'FFmpeg-build.txt')]:
    (destination / name).write_text(subprocess.check_output(
        [str(ffmpeg), option], text=True, stderr=subprocess.STDOUT))
# The pinned MacPorts variant is +gpl2, as recorded above. Include the full
# license text already used by this GPL-2.0 application as an explicit notice.
ffmpeg_license = (destination / 'FFmpeg-license.txt').read_text()
if 'GNU General Public License' not in ffmpeg_license or 'version 2' not in ffmpeg_license:
    raise SystemExit('Update the FFmpeg license bundle for this different build variant')
shutil.copy2('LICENSE', destination / 'FFmpeg-GPL-2.0.txt')
(destination / 'Sources.txt').write_text(
    'Memento source: https://github.com/OozoraGokuu/memento-custom\n'
    'Build commit: ' + subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True) +
    'MacPorts recipes: https://github.com/macports/macports-ports/tree/f1ba5ef34565b3116be5d7a2f97c20fe96c855d7\n'
    'MacPorts source archives: https://distfiles.macports.org/\n'
    'Qt source and licenses: https://download.qt.io/archive/qt/6.9/\n')

# libcurl and its DoH transport must work without the build machine's CA path.
for certificate in [Path('/opt/local/share/curl/curl-ca-bundle.crt'), Path('/etc/ssl/cert.pem')]:
    if certificate.is_file():
        shutil.copy2(certificate, bundle / 'Contents/Resources/cacert.pem')
        break
else:
    raise SystemExit('No CA bundle found for the standalone app')

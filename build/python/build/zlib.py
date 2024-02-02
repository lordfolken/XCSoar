import subprocess

from build.makeproject import MakeProject

class ZlibProject(MakeProject):
    def __init__(self, url, alternative_url, md5, installed,
                 **kwargs):
        MakeProject.__init__(self, url, alternative_url, md5, installed, **kwargs)

    def get_make_args(self, toolchain):
        return MakeProject.get_make_args(self, toolchain) + [
            f'CC={toolchain.cc} {toolchain.cppflags} {toolchain.cflags}',
            f'CPP={toolchain.cc} -E {toolchain.cppflags}',
            f'AR={toolchain.ar}',
            f'ARFLAGS={toolchain.arflags}',
            f'RANLIB={toolchain.ranlib}',
            f'LDSHARED={toolchain.cc} -shared',
            'libz.a',
        ]

    def get_make_install_args(self, toolchain):
        return [f'RANLIB={toolchain.ranlib}', self.install_target]

    def _build(self, toolchain):
        src = self.unpack(toolchain, out_of_tree=False)

        subprocess.check_call(
            ['./configure', f'--prefix={toolchain.install_prefix}', '--static'],
            cwd=src,
            env=toolchain.env,
        )
        self.build_make(toolchain, src)

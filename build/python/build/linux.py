from .makeproject import MakeProject

class SabotageLinuxHeadersProject(MakeProject):
    def get_make_install_args(self, toolchain):
        return MakeProject.get_make_install_args(self, toolchain) + [
            'ARCH=arm',
            f'prefix={toolchain.install_prefix}',
        ]

    def _build(self, toolchain):
        src = self.unpack(toolchain)
        self.make(toolchain, src, self.get_make_install_args(toolchain))

from .autotools import AutotoolsProject

class MuslProject(AutotoolsProject):
    def configure(self, toolchain, *args, **kwargs):
        if cross_compile := toolchain.ar.removesuffix('ar'):
            self.configure_args.append(f'CROSS_COMPILE={cross_compile}')

        return AutotoolsProject.configure(self, toolchain, *args, **kwargs)

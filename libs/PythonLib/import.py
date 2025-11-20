import cryptic
import sys

class CrypticImportHook:
    def __init__(self):
        pass

    def find_module(self, fullname, path=None):
        if fullname.startswith("cryptic."):
            return self

    def load_module(self, fullname):
        mod = sys.modules.get(fullname)
        is_pkg = False
        
        if mod:
            return mod
        
        mod_path = fullname[8:].replace('.', '/')
        pkg_path = mod_path + '/__init__.py'
        
        if cryptic.exists(pkg_path):
            is_pkg = True

        if is_pkg:
            mod = cryptic.load(pkg_path, fullname)
        elif cryptic.exists(mod_path):
            mod = cryptic.load(mod_path, fullname)
        else:
            return None

        if not mod:
            raise ImportError, "Error while loading a Cryptic module: %s" % fullname

        if is_pkg:
            mod.__path__ = []

        mod.__loader__ = self
        if '.' in fullname:
            parent, child = fullname.rsplit('.', 1)
            setattr(sys.modules[parent], child, mod)
        return mod

sys.meta_path.insert(0, CrypticImportHook())

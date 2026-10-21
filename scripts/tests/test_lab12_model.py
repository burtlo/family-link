from __future__ import annotations
import shutil, subprocess, tempfile, unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
PROJECT=ROOT/'client/labs/lab-12-profile-settings'

class Lab12ModelTests(unittest.TestCase):
    def test_lab12_model_native(self):
        compiler=shutil.which('cc') or shutil.which('gcc')
        self.assertIsNotNone(compiler,'C compiler required for native model check')
        with tempfile.TemporaryDirectory(prefix='lab12-model-') as temp:
            binary=Path(temp)/'test-model'
            subprocess.run([compiler,'-std=c11','-Wall','-Wextra','-Werror','-I',str(PROJECT/'components/lab12_model/include'),
                str(PROJECT/'components/lab12_model/lab12_model.c'),str(PROJECT/'components/lab12_model/lab12_profile_sync.c'),str(PROJECT/'tests/test_model_native.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

if __name__=='__main__': unittest.main()

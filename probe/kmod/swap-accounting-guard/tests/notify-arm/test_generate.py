import argparse
import contextlib
import io
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import generate


class GenerateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.module = Path(__file__).resolve().parents[2]
        cls.root = cls.module.parents[2]
        cls.temp = tempfile.TemporaryDirectory()
        cls.directory = Path(cls.temp.name)
        binary = cls.directory/'dump-spec'
        command = [os.environ.get('CC','gcc'),'-Wall','-Wextra','-Werror','-O1',str(cls.module/'tests/dump_spec.c'),'-o',str(binary)]
        result = subprocess.run(command,capture_output=True,text=True)
        if result.returncode:
            raise RuntimeError(str(command)+'\n'+result.stdout+result.stderr)
        cls.text = subprocess.check_output([str(binary)],text=True)
        cls.vmlinux = Path(os.environ.get('NW_VMLINUX',cls.root/'nwa50-extracted/kernel/vmlinux.bin'))
        cls.kallsyms = Path(os.environ.get('NW_KALLSYMS',cls.root/'work/android/wdt-nozram-20261012T062210Z/baseline/kallsyms.out'))

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.spec = self.directory/'spec.txt'
        self.spec.write_text(self.text)
        self.output = self.directory/'output'
        self.output.mkdir(exist_ok=True)
        self.args = argparse.Namespace(spec=self.spec,vmlinux=self.vmlinux,kallsyms=self.kallsyms,out=self.output)

    def bad_spec(self, transform):
        self.spec.write_text(transform(self.text))
        with self.assertRaises(ValueError):
            generate.read_spec(self.spec)

    def test_shared_spec(self):
        sites, targets = generate.read_spec(self.spec)
        self.assertEqual((len(sites),len(targets)),(22,5))

    def test_ftrace_alternate_only_at_mcount(self):
        sites, targets = generate.read_spec(self.spec)
        alternate = [site for site in sites if site['alternate']]
        self.assertEqual(len(alternate),1)
        self.assertEqual(alternate[0]['base']+alternate[0]['offset'],0xc03481e8)
        self.assertEqual(alternate[0]['alternate'],0xe8bd4000)

    def test_missing_ftrace_alternate(self):
        self.bad_spec(lambda text:text.replace('ebf31b28 ebf31b28 e8bd4000','ebf31b28 ebf31b28 00000000'))

    def test_wrong_ftrace_alternate(self):
        self.bad_spec(lambda text:text.replace('e8bd4000','e8bd4001'))

    def test_write_site_alternate(self):
        self.bad_spec(lambda text:text.replace('ebfe8e2c e3a00000 00000000','ebfe8e2c e3a00000 e8bd4000'))

    def test_other_readonly_alternate(self):
        self.bad_spec(lambda text:text.replace('e1a0c00d e1a0c00d 00000000','e1a0c00d e1a0c00d e8bd4000'))

    def test_ftrace_nop_candidate(self):
        with contextlib.redirect_stdout(io.StringIO()):generate.generate(self.args)
        for name in ['notify-stock','notify-trylock']:
            original=(self.output/(name+'.bin')).read_bytes()
            nop=(self.output/(name+'-nop.bin')).read_bytes()
            self.assertEqual(nop[:16],original[:16])
            self.assertEqual(nop[16:20],bytes.fromhex('0040bde8'))
            self.assertEqual(nop[20:],original[20:])
        original=(self.output/'rw-public.bin').read_bytes()
        nop=(self.output/'rw-public-nop.bin').read_bytes()
        self.assertEqual(sum(original[i:i+4]!=nop[i:i+4] for i in range(0,len(original),4)),2)
        self.assertEqual(nop[16:20],bytes.fromhex('0040bde8'))
        self.assertEqual(nop[72:76],bytes.fromhex('0040bde8'))

    def test_wrong_count(self):
        self.bad_spec(lambda text:text.replace('SPEC 22 11','SPEC 2 2'))

    def test_empty(self):
        self.bad_spec(lambda text:'')

    def test_malformed(self):
        self.bad_spec(lambda text:text+'garbage\n')

    def test_missing_target(self):
        self.bad_spec(lambda text:'\n'.join(text.splitlines()[:-1])+'\n')

    def test_duplicate_target(self):
        rows=self.text.splitlines();rows[-1]=rows[-2]
        self.bad_spec(lambda text:'\n'.join(rows)+'\n')

    def test_duplicate_site(self):
        rows=self.text.splitlines();rows[2]=rows[1]
        self.bad_spec(lambda text:'\n'.join(rows)+'\n')

    def test_unaligned(self):
        self.bad_spec(lambda text:text.replace('c01acabc d4','c01acabc d5'))

    def test_address_overflow(self):
        self.bad_spec(lambda text:text.replace('c01acabc d4','c01acabc 100000000'))

    def test_wrong_changed_count(self):
        self.bad_spec(lambda text:text.replace('ebfe8e2c e3a00000','ebfe8e2c ebfe8e2c'))

    def test_symbols_empty(self):
        path=self.directory/'bad-kall';path.write_text('')
        with self.assertRaises(ValueError):generate.kernel_symbols(path)

    def test_symbols_malformed(self):
        path=self.directory/'bad-kall';path.write_text('bad row\n')
        with self.assertRaises(ValueError):generate.kernel_symbols(path)

    def test_symbol_missing(self):
        with self.assertRaises(ValueError):generate.symbol([], 'missing')

    def test_symbol_duplicate(self):
        with self.assertRaises(ValueError):generate.symbol([(1,'x'),(2,'x')], 'x')

    def test_code_bounds(self):
        for start,end in [(0,4),(generate.BASE,generate.BASE),(generate.BASE,generate.BASE+10000)]:
            with self.assertRaises(ValueError):generate.slice_code(b'xxxx',start,end)

    def test_actual_candidate(self):
        with contextlib.redirect_stdout(io.StringIO()):generate.generate(self.args)
        self.assertEqual((self.output/'notify-stock.bin').stat().st_size,80)
        old=(self.output/'notify-stock.bin').read_bytes()
        new=(self.output/'notify-trylock.bin').read_bytes()
        self.assertEqual(old[:40],new[:40])
        self.assertEqual(old[-4:],new[-4:])
        self.assertEqual(sum(old[i:i+4]!=new[i:i+4] for i in range(0,80,4)),9)

    def test_wrong_image(self):
        path=self.directory/'bad-image';path.write_bytes(b'bad')
        self.args.vmlinux=path
        with self.assertRaisesRegex(ValueError,'stock image'):generate.generate(self.args)

    def test_stock_word_mismatch(self):
        self.spec.write_text(self.text.replace('ebfe8e2c e3a00000','ebfe8e2d e3a00000'))
        with self.assertRaisesRegex(ValueError,'stock word mismatch'):generate.generate(self.args)

    def test_target_address_mismatch(self):
        self.spec.write_text(self.text.replace('TARGET down_write_trylock c007bc4c','TARGET down_write_trylock c007bc50'))
        with self.assertRaisesRegex(ValueError,'target address mismatch'):generate.generate(self.args)

    def test_site_address_mismatch(self):
        self.spec.write_text(self.text.replace('c03481d8 4c','c03481dc 4c'))
        with self.assertRaisesRegex(ValueError,'site symbol mismatch'):generate.generate(self.args)

    def test_callback_span_missing(self):
        self.spec.write_text(self.text.replace('zram_slot_free_notify c03481d8 4c','zram_slot_free_notify c03481d8 50'))
        with self.assertRaisesRegex(ValueError,'stock word mismatch|callback guard span'):generate.generate(self.args)


if __name__=='__main__':
    unittest.main()

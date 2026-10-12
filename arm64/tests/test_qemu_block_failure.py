#!/usr/bin/env python3
"""Real QEMU blkdebug EIO: isolate a failed controller and boot disk1."""
from pathlib import Path
import subprocess
import sys
import tempfile

from test_qemu_multiboot import ARM, CREATE, STAGE, SUCCESS, must, run


def main():
    with tempfile.TemporaryDirectory(prefix='seabios-block-fault-') as tmp:
        work = Path(tmp)
        disk = work / 'stage.img'
        subprocess.run([sys.executable, str(CREATE), str(STAGE), str(disk)],
                       check=True, stdout=subprocess.DEVNULL)
        for mode in ('v1', 'v2'):
            # LBA0 exercises discovery isolation. The second file cluster
            # fails after a candidate is registered and its load has begun.
            for case, sector in [('discovery', 0), ('stage-load', 2048 + 32 + 650 + 4)]:
                config = work / 'fault.conf'
                # Modern raw-format does not emit read_aio debug events.
                # Activate at open, then restrict by read operation/sector.
                config.write_text('[inject-error]\nevent = "none"\niotype = "read"\n'
                                  f'errno = "5"\nsector = "{sector}"\nonce = "off"\n')
                text = run([disk, f'blkdebug:{config}:{disk}'], mode == 'v2')
                (ARM / 'out' / f'block-fault-{mode}-{case}.log').write_text(text)
                must(text, '[virtio-blk] controller failed; isolated disk 0', mode)
                must(text, '[boot] legacy drive index 1', mode)
                must(text, '[virtio-blk] all queues reset before native handoff', mode)
                must(text, SUCCESS, mode)
                if case == 'stage-load':
                    must(text, '[boot] legacy drive index 0', mode)
                print(f'PASS: {mode} {case} EIO isolated; disk1 native boot succeeds', flush=True)


if __name__ == '__main__':
    main()

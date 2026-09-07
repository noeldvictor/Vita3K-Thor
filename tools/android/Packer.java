import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

/**
 * Thor: pack folders into a STORED (uncompressed) zip, on the device.
 *
 * Runs under app_process with nothing but the Android runtime, so the SD card
 * never has to round-trip through a PC. Stored members are what the emulator
 * reads in place at any offset; a deflated member would have to be unpacked
 * into the cartridge cache first. Usage:
 *
 *   CLASSPATH=/data/local/tmp/packer.jar ANDROID_DATA=/data/local/tmp \
 *     app_process /data/local/tmp Packer <out.zip> <prefix>=<dir> [<prefix>=<dir> ...]
 *
 * Every regular file under <dir> becomes <prefix>/<relative path>. The zip is
 * written to <out.zip>.part and renamed at the end, so a half-written archive
 * never looks like a finished one. tools/pack_cartridges.py drives this.
 */
public class Packer {
    public static void main(String[] args) throws Exception {
        if (args.length < 2) {
            System.err.println("usage: Packer <out.zip> <prefix>=<dir> [<prefix>=<dir> ...]");
            System.exit(2);
        }

        final File out = new File(args[0]);
        final File part = new File(args[0] + ".part");
        final byte[] buffer = new byte[4 << 20];
        long files = 0;
        long bytes = 0;

        try (ZipOutputStream zip = new ZipOutputStream(new BufferedOutputStream(new FileOutputStream(part), 8 << 20))) {
            zip.setMethod(ZipOutputStream.STORED);
            for (int i = 1; i < args.length; i++) {
                final int eq = args[i].indexOf('=');
                if (eq <= 0) {
                    System.err.println("bad argument, want <prefix>=<dir>: " + args[i]);
                    System.exit(2);
                }
                final String prefix = args[i].substring(0, eq);
                final File dir = new File(args[i].substring(eq + 1));
                final List<File> list = new ArrayList<>();
                walk(dir, list);
                Collections.sort(list);

                for (File file : list) {
                    final String relative = dir.toPath().relativize(file.toPath()).toString().replace('\\', '/');
                    final ZipEntry entry = new ZipEntry(prefix + "/" + relative);

                    // STORED needs size and CRC up front, so read the file twice.
                    final CRC32 crc = new CRC32();
                    long length = 0;
                    try (InputStream in = new BufferedInputStream(new FileInputStream(file), 1 << 20)) {
                        int n;
                        while ((n = in.read(buffer)) > 0) {
                            crc.update(buffer, 0, n);
                            length += n;
                        }
                    }
                    entry.setMethod(ZipEntry.STORED);
                    entry.setSize(length);
                    entry.setCompressedSize(length);
                    entry.setCrc(crc.getValue());
                    entry.setTime(file.lastModified());

                    zip.putNextEntry(entry);
                    try (InputStream in = new BufferedInputStream(new FileInputStream(file), 1 << 20)) {
                        int n;
                        while ((n = in.read(buffer)) > 0)
                            zip.write(buffer, 0, n);
                    }
                    zip.closeEntry();

                    files++;
                    bytes += length;
                    if (files % 250 == 0)
                        System.out.println("progress " + files + " files " + (bytes >> 20) + " MiB");
                }
            }
        }

        if (!part.renameTo(out)) {
            System.err.println("could not rename " + part + " to " + out);
            System.exit(1);
        }
        System.out.println("done " + files + " files " + bytes + " bytes -> " + out);
    }

    private static void walk(File dir, List<File> list) {
        final File[] children = dir.listFiles();
        if (children == null)
            return;
        for (File child : children) {
            if (child.isDirectory())
                walk(child, list);
            else if (child.isFile())
                list.add(child);
        }
    }
}

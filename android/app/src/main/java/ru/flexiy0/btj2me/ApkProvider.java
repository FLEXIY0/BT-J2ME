package ru.flexiy0.btj2me;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;

/* Отдаёт системной установке скачанный update.apk (Android 7+ не принимает file://).
 * Без AndroidX: провайдер на один файл, только чтение. */
public class ApkProvider extends ContentProvider {
    static Uri uri(Context c) {
        return Uri.parse("content://" + c.getPackageName() + ".update/update.apk");
    }

    private File file() {
        File base = getContext().getExternalFilesDir(null);
        return new File(base != null ? base : getContext().getFilesDir(), "update.apk");
    }

    @Override
    public boolean onCreate() { return true; }

    @Override
    public String getType(Uri u) { return "application/vnd.android.package-archive"; }

    @Override
    public ParcelFileDescriptor openFile(Uri u, String mode) throws FileNotFoundException {
        if (!"update.apk".equals(u.getLastPathSegment()) || !"r".equals(mode))
            throw new FileNotFoundException();
        return ParcelFileDescriptor.open(file(), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public Cursor query(Uri u, String[] proj, String sel, String[] args, String order) {
        MatrixCursor c = new MatrixCursor(new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE });
        c.addRow(new Object[] { "update.apk", file().length() });
        return c;
    }

    @Override
    public Uri insert(Uri u, ContentValues v) { return null; }

    @Override
    public int delete(Uri u, String s, String[] a) { return 0; }

    @Override
    public int update(Uri u, ContentValues v, String s, String[] a) { return 0; }
}

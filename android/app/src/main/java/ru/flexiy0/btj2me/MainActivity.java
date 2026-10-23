package ru.flexiy0.btj2me;

import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

import org.libsdl.app.SDLActivity;

/* Окно SDL с лаунчером и играми + импорт JAR-ов в папку игр:
 * плитка «+» на полке (системный выбор файлов) и «Открыть с помощью» из других приложений. */
public class MainActivity extends SDLActivity {
    private static final String TAG = "bt-j2me";
    private static final int PICK_GAMES = 4242;

    /* native: в папке игр новое — лаунчер перечитает полку */
    private static native void nativeGamesChanged();

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        handleIntent(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleIntent(intent);
    }

    /* Зовётся из C (platform_import_games) в потоке игры. */
    public void importGames() {
        runOnUiThread(new Runnable() {
            public void run() {
                Intent i;
                if (Build.VERSION.SDK_INT >= 19) {
                    i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                    i.putExtra(Intent.EXTRA_MIME_TYPES, new String[] { "application/java-archive",
                            "application/x-java-archive", "text/vnd.sun.j2me.app-descriptor",
                            "application/octet-stream", "application/zip" });
                } else {
                    i = new Intent(Intent.ACTION_GET_CONTENT);   /* Android 4.1–4.3: обычный выбор файла */
                }
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                if (Build.VERSION.SDK_INT >= 18)
                    i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                try {
                    startActivityForResult(Intent.createChooser(i, null), PICK_GAMES);
                } catch (Exception e) {
                    Log.w(TAG, "нет выбора файлов", e);
                }
            }
        });
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_GAMES || result != RESULT_OK || data == null)
            return;
        List<Uri> uris = new ArrayList<Uri>();
        ClipData clip = data.getClipData();
        if (clip != null)
            for (int i = 0; i < clip.getItemCount(); i++)
                uris.add(clip.getItemAt(i).getUri());
        else if (data.getData() != null)
            uris.add(data.getData());
        copyInBackground(uris);
    }

    private void handleIntent(Intent intent) {
        if (intent == null)
            return;
        List<Uri> uris = new ArrayList<Uri>();
        if (Intent.ACTION_VIEW.equals(intent.getAction()) && intent.getData() != null)
            uris.add(intent.getData());
        else if (Intent.ACTION_SEND.equals(intent.getAction())) {
            Object u = intent.getParcelableExtra(Intent.EXTRA_STREAM);
            if (u instanceof Uri)
                uris.add((Uri) u);
        }
        if (!uris.isEmpty())
            copyInBackground(uris);
    }

    private File gamesDir() {
        File base = getExternalFilesDir(null);
        File dir = new File(base != null ? base : getFilesDir(), "games");
        dir.mkdirs();
        return dir;
    }

    private void copyInBackground(final List<Uri> uris) {
        new Thread(new Runnable() {
            public void run() {
                int copied = 0;
                for (Uri u : uris)
                    if (copy(u))
                        copied++;
                if (copied > 0) {
                    try {
                        nativeGamesChanged();
                    } catch (UnsatisfiedLinkError e) {
                        // библиотека ещё не загружена — полка перечитается при запуске сама
                    }
                }
            }
        }, "import").start();
    }

    private String displayName(Uri uri) {
        String name = null;
        if ("content".equals(uri.getScheme())) {
            Cursor c = null;
            try {
                c = getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null);
                if (c != null && c.moveToFirst())
                    name = c.getString(0);
            } catch (Exception e) {
                // у некоторых провайдеров имени нет
            } finally {
                if (c != null)
                    c.close();
            }
        }
        if (name == null)
            name = uri.getLastPathSegment();
        if (name == null)
            name = "game";
        return name.replace('/', '_').replace('\\', '_');
    }

    /* Копирует JAR или JAD в папку игр. Файл без расширения, но с ZIP внутри, считаем JAR-ом. */
    private boolean copy(Uri uri) {
        String name = displayName(uri);
        String lower = name.toLowerCase(Locale.ROOT);
        InputStream in = null;
        OutputStream out = null;
        File tmp = null;
        try {
            in = getContentResolver().openInputStream(uri);
            if (in == null)
                return false;
            byte[] buf = new byte[65536];
            int n = in.read(buf);
            if (n <= 0)
                return false;
            boolean zip = n >= 2 && buf[0] == 'P' && buf[1] == 'K';
            if (!lower.endsWith(".jar") && !lower.endsWith(".jad")) {
                if (!zip)
                    return false;
                name = name + ".jar";
            }
            File dir = gamesDir();
            tmp = new File(dir, name + ".part");
            out = new FileOutputStream(tmp);
            do {
                out.write(buf, 0, n);
            } while ((n = in.read(buf)) > 0);
            out.close();
            out = null;
            File dst = new File(dir, name);
            dst.delete();
            if (!tmp.renameTo(dst))
                return false;
            Log.i(TAG, "импорт: " + dst);
            return true;
        } catch (Exception e) {
            Log.w(TAG, "не удалось скопировать " + uri, e);
            if (tmp != null)
                tmp.delete();
            return false;
        } finally {
            try {
                if (in != null)
                    in.close();
                if (out != null)
                    out.close();
            } catch (Exception e) {
                // ничего
            }
        }
    }
}

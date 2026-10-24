package ru.flexiy0.btj2me;

import android.content.ClipData;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
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

    /* MIDlet.platformRequest: ссылку открывает браузер (sms:/tel: — приложения сообщений и звонков). */
    public boolean openUrl(String url) {
        try {
            Intent i = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            startActivity(i);
            return true;
        } catch (Exception e) {
            Log.w(TAG, "не открыть " + url, e);
            return false;
        }
    }

    /* ---------- обновление (src/update.c зовёт из своего потока) ---------- */

    private static HttpURLConnection open(String url) throws Exception {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        if (c instanceof javax.net.ssl.HttpsURLConnection && Build.VERSION.SDK_INT < 21)
            ((javax.net.ssl.HttpsURLConnection) c).setSSLSocketFactory(new Tls12SocketFactory());
        c.setConnectTimeout(15000);
        c.setReadTimeout(30000);
        c.setInstanceFollowRedirects(true);
        c.setRequestProperty("User-Agent", "BT-J2ME");
        c.setRequestProperty("Accept", "application/vnd.github+json, */*");
        return c;
    }

    /* GitHub отдаёт файл выпуска переадресацией на другой хост — идём по ней сами. */
    private static HttpURLConnection follow(String url) throws Exception {
        for (int i = 0; i < 5; i++) {
            HttpURLConnection c = open(url);
            int code = c.getResponseCode();
            if (code / 100 != 3)
                return code == 200 ? c : null;
            url = new URL(new URL(url), c.getHeaderField("Location")).toString();
            c.disconnect();
        }
        return null;
    }

    public byte[] httpGet(String url) {
        try {
            HttpURLConnection c = follow(url);
            if (c == null)
                return null;
            InputStream in = c.getInputStream();
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) > 0 && out.size() < (1 << 20))
                out.write(buf, 0, n);
            in.close();
            return out.toByteArray();
        } catch (Exception e) {
            Log.w(TAG, "не скачать " + url, e);
            return null;
        }
    }

    public boolean download(String url, String path) {
        File f = new File(path);
        try {
            HttpURLConnection c = follow(url);
            if (c == null)
                return false;
            InputStream in = c.getInputStream();
            OutputStream out = new FileOutputStream(f);
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0)
                out.write(buf, 0, n);
            out.close();
            in.close();
            return true;
        } catch (Exception e) {
            Log.w(TAG, "не скачать " + url, e);
            f.delete();
            return false;
        }
    }

    /* Системная установка скачанного APK: новая версия встаёт поверх, данные остаются. */
    public void installApk(final String path) {
        runOnUiThread(new Runnable() {
            public void run() {
                try {
                    Intent i = new Intent(Intent.ACTION_VIEW);
                    Uri uri = Build.VERSION.SDK_INT >= 24 ? ApkProvider.uri(MainActivity.this)
                                                          : Uri.fromFile(new File(path));
                    i.setDataAndType(uri, "application/vnd.android.package-archive");
                    i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    startActivity(i);
                } catch (Exception e) {
                    Log.w(TAG, "не открыть установку " + path, e);
                }
            }
        });
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
                            "application/octet-stream", "application/zip", "application/x-nes-rom",
                            "application/x-cue", "application/x-cd-image", "application/x-iso9660-image",
                            "application/x-psp-pbp", "text/plain" });
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
                    if (copy(u)) {
                        copied++;
                        copySiblings(u);
                    }
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

    private static boolean isDisc(String lower) {
        return lower.endsWith(".cue") || lower.endsWith(".bin") || lower.endsWith(".iso") ||
               lower.endsWith(".img") || lower.endsWith(".pbp") || lower.endsWith(".chd") ||
               lower.endsWith(".cso") || lower.endsWith(".zso");
    }

    /* Дорожки, которые называет .cue (FILE "…"). */
    private static List<String> cueFiles(File cue) {
        List<String> names = new ArrayList<String>();
        try {
            java.io.BufferedReader r = new java.io.BufferedReader(new java.io.FileReader(cue));
            String line;
            while ((line = r.readLine()) != null) {
                line = line.trim();
                if (!line.regionMatches(true, 0, "FILE ", 0, 5))
                    continue;
                line = line.substring(5).trim();
                int end = line.startsWith("\"") ? line.indexOf('"', 1) : line.indexOf(' ');
                String f = line.startsWith("\"") ? (end > 0 ? line.substring(1, end) : null)
                                                 : (end > 0 ? line.substring(0, end) : line);
                if (f != null && f.length() > 0)
                    names.add(new File(f.replace('\\', '/')).getName());
            }
            r.close();
        } catch (Exception e) {
            // не текст — нечего искать
        }
        return names;
    }

    /* Диск PS1 открыли файлом (file://): к .cue забираем его дорожки, к .bin — одноимённый .cue.
     * У content:// соседей не видно — тогда .cue и .bin выбирают вместе в «Добавить игры». */
    private void copySiblings(Uri uri) {
        if (!"file".equals(uri.getScheme()) || uri.getPath() == null)
            return;
        File src = new File(uri.getPath());
        String lower = src.getName().toLowerCase(Locale.ROOT);
        File dir = gamesDir();
        if (lower.endsWith(".bin")) {
            String stem = src.getName().substring(0, src.getName().length() - 4);
            File cue = new File(src.getParentFile(), stem + ".cue");
            if (cue.exists())
                copy(Uri.fromFile(cue));
        }
        if (lower.endsWith(".cue") || lower.endsWith(".bin")) {
            File cue = lower.endsWith(".cue") ? new File(dir, src.getName())
                    : new File(dir, src.getName().substring(0, src.getName().length() - 4) + ".cue");
            for (String f : cueFiles(cue)) {
                File track = new File(src.getParentFile(), f);
                if (track.exists() && !new File(dir, f).exists())
                    copy(Uri.fromFile(track));
            }
        }
    }

    /* Копирует JAR, JAD, картридж NES или диск PS1 в папку игр. Файл без расширения узнаём
     * по началу: ZIP — JAR, «NES\x1A» — картридж, «\0PBP» — диск PS1 из PSN.
     * BIOS PlayStation (.bin ровно 512 КБ) кладём в папку bios рядом с играми. */
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
            boolean nes = n >= 4 && buf[0] == 'N' && buf[1] == 'E' && buf[2] == 'S' && buf[3] == 0x1A;
            boolean pbp = n >= 4 && buf[0] == 0 && buf[1] == 'P' && buf[2] == 'B' && buf[3] == 'P';
            if (!lower.endsWith(".jar") && !lower.endsWith(".jad") && !lower.endsWith(".nes") && !isDisc(lower)) {
                if (nes)
                    name = name + ".nes";      /* картридж NES без расширения */
                else if (pbp)
                    name = name + ".pbp";
                else if (zip)
                    name = name + ".jar";
                else
                    return false;
            }
            File dir = gamesDir();
            tmp = new File(dir, name + ".part");
            out = new FileOutputStream(tmp);
            do {
                out.write(buf, 0, n);
            } while ((n = in.read(buf)) > 0);
            out.close();
            out = null;
            if (lower.endsWith(".bin") && tmp.length() == 512 * 1024) {
                dir = new File(dir, "bios");
                dir.mkdirs();
            }
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

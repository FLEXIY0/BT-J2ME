package javax.microedition.m3g;

/* M3G (JSR-184) — только то, чем игры рисуют 2D: спрайты Sprite3D в мире с камерой,
 * фон и смешивание. Сеток (Mesh) и загрузчика .m3g здесь нет. */
public abstract class Object3D {
    int userID;
    Object userObject;

    public int getUserID() { return userID; }
    public void setUserID(int id) { userID = id; }
    public Object getUserObject() { return userObject; }
    public void setUserObject(Object o) { userObject = o; }

    public Object3D find(int id) {
        return userID == id ? this : null;
    }

    public int getReferences(Object3D[] refs) { return 0; }
    public int animate(int time) { return 0; }
    public int getAnimationTrackCount() { return 0; }

    public final Object3D duplicate() {
        throw new IllegalStateException("duplicate не поддерживается");
    }
}

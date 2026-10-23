package javax.microedition.m3g;

import java.util.Vector;

public class Group extends Node {
    final Vector children = new Vector();

    public void addChild(Node child) {
        if (child == null)
            throw new NullPointerException();
        if (child == this || child instanceof World || (child.parent != null && child.parent != this))
            throw new IllegalArgumentException();
        if (child.parent == this)
            return;
        child.parent = this;
        children.addElement(child);
    }

    public void removeChild(Node child) {
        if (child != null && children.removeElement(child))
            child.parent = null;
    }

    public int getChildCount() { return children.size(); }
    public Node getChild(int i) { return (Node) children.elementAt(i); }

    public Object3D find(int id) {
        if (userID == id)
            return this;
        for (int i = 0; i < children.size(); i++) {
            Object3D o = ((Node) children.elementAt(i)).find(id);
            if (o != null)
                return o;
        }
        return null;
    }
}

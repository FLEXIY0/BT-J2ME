package javax.microedition.lcdui;

public interface Choice {
    int EXCLUSIVE = 1, MULTIPLE = 2, IMPLICIT = 3, POPUP = 4;
    int TEXT_WRAP_DEFAULT = 0, TEXT_WRAP_ON = 1, TEXT_WRAP_OFF = 2;

    int size();
    String getString(int i);
    Image getImage(int i);
    int append(String s, Image img);
    void insert(int i, String s, Image img);
    void delete(int i);
    void deleteAll();
    void set(int i, String s, Image img);
    boolean isSelected(int i);
    int getSelectedIndex();
    int getSelectedFlags(boolean[] flags);
    void setSelectedIndex(int i, boolean selected);
    void setSelectedFlags(boolean[] flags);
    void setFitPolicy(int policy);
    int getFitPolicy();
    void setFont(int i, Font f);
    Font getFont(int i);
}

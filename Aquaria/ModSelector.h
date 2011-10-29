#ifndef AQ_MOD_SELECTOR_H
#define AQ_MOD_SELECTOR_H

#include "AquariaMenuItem.h"

class JuicyProgressBar : public Quad
{
public:
    JuicyProgressBar();

    void progress(float p);
    void showText(bool b);
    void setText(const std::string& s);
    inline float progress() { return perc; }
    BitmapText txt;
    Quad juice;

protected:
    float perc;
};

class BasicIcon : public AquariaGuiQuad
{
public:
    BasicIcon();
    std::string label;

protected:
    bool mouseDown;
    Vector scaleNormal;
    Vector scaleBig;
    virtual void onUpdate(float dt);
    virtual void onClick();
};

class SubtitleIcon : public BasicIcon
{
protected:
    virtual void onUpdate(float dt);
};

class ModIcon : public SubtitleIcon
{
public:
    ModIcon();
    void loadEntry(const ModEntry& entry);
    virtual void updateStatus();
    std::string fname; // internal mod name (file/folder name)
    std::string modname; // mod name as given by author
    unsigned int modId;
    ModType modType;

protected:
    virtual void onClick();
};

class ModIconOnline : public SubtitleIcon
{
public:
    ModIconOnline(const std::string& name, const std::string& desc, const std::string& icon, const std::string& pkg);
    bool fixIcon();
    std::string fname, iconfile, packageUrl, localPackageName;
    std::string confirmStr;
    void setDownloadProgress(float p, float barheight = 20);
    virtual void updateStatus();
    JuicyProgressBar *pb;
    Quad *extraIcon;
    bool clickable;

protected:
    virtual void onClick();
};

class MenuIcon : public SubtitleIcon
{
public:
    typedef void (*callback)(int, void*);
    MenuIcon(int id);

    callback cb;
    void *cb_data;

protected:
    int iconId;
    virtual void onClick();
};

class MenuBasicBar : public AquariaGuiQuad
{
public:
    MenuBasicBar();
    virtual void init();
};

class MenuIconBar : public MenuBasicBar
{
public:
    virtual void init();
    std::vector<MenuIcon*> icons;

protected:
    void add(MenuIcon *ico);
};

class MenuArrowBar : public MenuBasicBar
{
public:
    virtual void init();
};


class IconGridPanel : public AquariaGuiQuad
{
public:
    IconGridPanel();
    void fade(bool in, bool sc);
    void add(RenderObject *obj);
    int spacing;

protected:
    int x, y; 
};

class ModSelectorScreen : public Quad
{
public:
    ModSelectorScreen();

    void init();
    void close();

    void showPanel(int id);
    void updateFade();
    
    void initModAndPatchPanel();
    void initNetPanel();

    std::vector<IconGridPanel*> panels;

protected:
    virtual void onUpdate(float dt);
    MenuIconBar leftbar;
    MenuArrowBar rightbar;
    int currentPanel;

    bool gotServerList;
};

#endif

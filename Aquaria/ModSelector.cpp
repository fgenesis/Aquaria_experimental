/*
Copyright (C) 2007, 2010 - Bit-Blot

This file is part of Aquaria.

Aquaria is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/
#include "../BBGE/DebugFont.h"

#include "DSQ.h"
#include "AquariaProgressBar.h"
#include "tinyxml.h"
#include "ModSelector.h"
#include "Network.h"
#include "LVPAFile.h"
#include "lvpa/MyCrc32.h"

enum ModDownloadType
{
    MODDL_SERVERLIST, // NYI
    MODDL_MODLIST,
    MODDL_MODICON,
    MODDL_PACKAGE,
};

#define MOD_ICON_SIZE 150
#define MAX_SERVER_HOPS 3


static bool _modname_cmp(const ModIcon *a, const ModIcon *b)
{
    return a->fname < b->fname;
}

static bool _CreateLocalPackageName(const std::string& url, std::string& plainName, std::string& localName) // URL or path
{
    size_t pos = url.find_last_of('/');
    size_t pos2 = url.find_last_of('.');
    if(pos != std::string::npos && pos2 != std::string::npos)
    {
        plainName = url.substr(pos+1, pos2-pos-1);
        localName = "_mods/" + plainName + ".aqmod";
        return true;
    }
    return false;
}

// OMG HACKS
static bool _ComparePackageURL(ModIconOnline *ico, const std::string& n)
{
   return ico->packageUrl == n;
}
static bool _CompareLocalIcon(ModIconOnline *ico, const std::string& n)
{
    return ico->iconfile == n;
}
static ModIconOnline *_FindModIconOnlineByFunction(const std::string& n, bool (*func)(ModIconOnline*,const std::string&))
{
    ModSelectorScreen* scr = dsq->modSelectorScr;
    IconGridPanel *grid = scr? scr->panels[2] : NULL;
    if(!grid)
        return NULL;

    for(RenderObject::Children::iterator it = grid->children.begin(); it != grid->children.end(); ++it)
    {
        ModIconOnline *ico = dynamic_cast<ModIconOnline*>(*it);
        if(!ico)
            continue;
        if(func(ico, n))
            return ico;
    }
    return NULL;
}

static void _FetchModList(const std::string& url, int hop);

// callback when a file is received
static void wget_notify(bool complete, int gotbytes, int totalbytes, void *user1, int user2, const std::string& user3)
{
    // -1 in case of error or finish, perc 0...1 otherwise
    bool abort = complete && !gotbytes && !totalbytes;
    float perc = (abort || complete) ? -1 : (float(gotbytes) / float(totalbytes));

    /*std::ostringstream os;
    os << "WGET: [" << complete << "] " << perc << " %; got " << gotbytes << "; total " << totalbytes << "; u1: " << user1 << "; u2: " << user2 << "; u3: " << user3;
    debugLog(os.str());*/

    if(abort)
        dsq->screenMessage("Download unsuccessful: " + user3); // DEBUG

    switch(user2) // ModDownloadType
    {
        case MODDL_SERVERLIST:
            break; // TODO FIXME

        case MODDL_MODLIST:
        {
            if(!complete || abort)
                return;
            
            ModSelectorScreen* scr = dsq->modSelectorScr;
            IconGridPanel *grid = scr? scr->panels[2] : NULL;

            TiXmlDocument d;
            if(!d.LoadFile(user3))
            {
                debugLog("ModSelectorScreen::initNetGrid() -- FAILED to parse mod list XML");
                return;
            }

            // parse main mod list
            /*
            <ModList>
                <Server url="example.com/mods.xml" /> //-- Server network - link to other servers
                ...
                <AquariaMod>
                    <Fullname text="Jukebox"/>
                    <Description text="Listen to all the songs in the game!"/>
                    <Icon url="localhost/aq/jukebox.png" />
                    <Package url="localhost/aq/jukebox.aqmod" />
                    <Author name="Dolphin's Cry" />  //-- optional tag // TODO
                    <Confirm text="" />  //-- optional tag, pops up confirm dialog // TODO
                    <Properties type="patch" /> //-- optional tag, if not given, "mod" is assumed.
                                                //   valid is also "patch", and (TODO) maybe more
                </AquariaMod>
                ...
            <ModList>
            */

            TiXmlElement *modlist = d.FirstChildElement("ModList");
            if(!modlist)
                break; // whoops?

            TiXmlElement *servx = modlist->FirstChildElement("Server");
            while(servx)
            {
                if(const char *url = servx->Attribute("url"))
                    _FetchModList(url, int((size_t)user1)); // this was abused as int, so this conversion is legal in this case

                servx = servx->NextSiblingElement("Server");
            }


            TiXmlElement *modx = modlist->FirstChildElement("AquariaMod");
            while(modx)
            {
                std::string namestr, descstr, iconurl, pkgurl, confirmStr;
                TiXmlElement *fullname, *desc, *icon, *pkg, *confirm;
                fullname = modx->FirstChildElement("Fullname");
                desc = modx->FirstChildElement("Description");
                icon = modx->FirstChildElement("Icon");
                pkg = modx->FirstChildElement("Package");
                confirm = modx->FirstChildElement("Confirm");

                if(fullname && fullname->Attribute("text"))
                    namestr = fullname->Attribute("text");

                if(desc && desc->Attribute("text"))
                    descstr = desc->Attribute("text");

                if(icon && icon->Attribute("url"))
                    iconurl = icon->Attribute("url");

                if(pkg && pkg->Attribute("url"))
                    pkgurl = pkg->Attribute("url");

                if(confirm && confirm->Attribute("text"))
                    confirmStr = confirm->Attribute("text");

                modx = modx->NextSiblingElement("AquariaMod");

                // -------------------

                if (descstr.size() > 255)
                    descstr.resize(255);

                const char *sl = strrchr(iconurl.c_str(), '/');
                if(!sl)
                    continue; // EH?

                std::string localIcon("gfx/?cache");
                localIcon += sl; // has '/'

                debugLog("NetMods: " + namestr);

                ModIconOnline *ico = NULL;
                if(grid)
                {
                    ico = new ModIconOnline(namestr, descstr, localIcon, pkgurl);
                    ico->confirmStr = confirmStr;
                    _CreateLocalPackageName(pkgurl, namestr, ico->localPackageName);  // use namestr as dummy, its not used below
                    grid->add(ico);
                }
                if(!ico->fixIcon()) // try to set texture, if its not there, download it
                {
                    if(ico)
                        ico->setDownloadProgress(0, 10);
                    Network::download(iconurl, localIcon, true, false, wget_notify, NULL, MODDL_MODICON, localIcon.c_str());
                    // we do not pass the ico ptr to the call above; otherwise it will crash if the mod menu is closed
                    // while a download is in progress
                }
            }
        }
        break;

        case MODDL_MODICON:
        {
            if(abort)
                return;
            ModIconOnline *ico = _FindModIconOnlineByFunction(user3, _CompareLocalIcon);
            if(ico)
            {
                if(complete)
                    ico->fixIcon();
                ico->setDownloadProgress(perc, 10); // must be done after setting the new texture for proper visuals
            }
        }
        break;

        case MODDL_PACKAGE:
        {


            ModIconOnline *ico = _FindModIconOnlineByFunction(user3, _ComparePackageURL);
            if(!ico)
                return; // happens if download is continuing in background and mod selector closed

            ico->setDownloadProgress(perc);

            ico->clickable = (abort || complete);

            if(abort)
            {
                dsq->sound->playSfx("denied");
                break;
            }

            if(complete)
            {
                const std::string& pkg = ico->localPackageName;
                std::string dummy, plainName;
                _CreateLocalPackageName(pkg, plainName, dummy);
                dsq->mountModPackage(pkg); // make package readable (so that the icon can be shown)
                if(!dsq->modIsKnown(plainName))
                {
                    // yay, got something new!
                    DSQ::loadModsCallback(pkg, 0); // does not end in ".xml" but thats no problem here
                    if(dsq->modSelectorScr)
                        dsq->modSelectorScr->initModAndPatchPanel(); // HACK
                }
                dsq->sound->playSfx("gem-collect");
            }
        }
        break;

        // ...
    }
}

static void _FetchModList(const std::string& url, int hop)
{
    std::ostringstream os;
    os << "Fetching mods list [" << url << "], remaining hops: " << hop;
    if(!hop)
        os << " -- IGNORING";
    debugLog(os.str());

    if(!hop)
        return; // reached limit. this is to prevent endless uncontrollable server chains, and possible recursion

    std::stringstream localName;
    localName << "?cache/mod_list_"
        << lvpa::CRC32::Calc(url.c_str(), url.length()) // just to have some variation
        << ".xml";

    debugLog("... to: " + localName.str());
    --hop;
    // HACK: abuse void* as int
    if(!Network::download(url, localName.str(), true, false, wget_notify, (void*)hop, MODDL_MODLIST, localName.str().c_str()))
    {
        debugLog("_FetchModList -- FAILED to get file from server: " + url);
    }
}

ModSelectorScreen::ModSelectorScreen() : Quad(), currentPanel(-1), gotServerList(false)
{
    followCamera = 1;
    shareAlphaWithChildren = false;
    alpha = 1;
    alphaMod = 0.1f;
    color = 0;
}

void ModSelectorScreen::onUpdate(float dt)
{
    Quad::onUpdate(dt);

    // mouse wheel scroll
    if(dsq->mouse.scrollWheelChange)
    {
        IconGridPanel *grid = panels[currentPanel];
        InterpolatedVector& v = grid->position;
        float ch = dsq->mouse.scrollWheelChange * 42;
        if(v.isInterpolating())
        {
            v.data->from = v;
            v.data->target.y += ch;
            v.data->timePassed = 0;
        }
        else
        {
            Vector v2 = grid->position;
            v2.y += ch;
            grid->position.interpolateTo(v2, 0.2f, 0, false, true);
        }
        //grid->position = Vector(grid->position.x, grid->position.y + (dsq->mouse.scrollWheelChange * 32));
    }
}

void ModSelectorScreen::showPanel(int id)
{
    if(id == currentPanel)
        return;

    const float t = 0.2f;
    IconGridPanel *newgrid = panels[id];

    // fade in selected panel
    if(currentPanel < 0) // just bringing up?
    {
        newgrid->scale = Vector(0.8f,0.8f);
        newgrid->alpha = 0;
    }
    newgrid->fade(true, true);

    currentPanel = id;

    updateFade();
}

void ModSelectorScreen::updateFade()
{
    // fade out background panels
    // necessary to do all of them, that icon alphas are 0... they would trigger otherwise, even if invisible because parent panel is not shown
    for(int i = 0; i < panels.size(); ++i)
        if(i != currentPanel)
            panels[i]->fade(false, true);
}

static void _MenuIconClickCallback(int id, void *user)
{
    ModSelectorScreen *ms = (ModSelectorScreen*)user;
    switch(id) // see MenuIconBar::init()
    {
        case 2: // network
            ms->initNetPanel();
            break;

        case 3: // exit
            dsq->quitNestedMain();
            return;
    }

    ms->showPanel(id);
}

// can be called multiple times without causing trouble
void ModSelectorScreen::init()
{
    leftbar.width = 100;
    leftbar.height = height;
    leftbar.alpha = 0;
    leftbar.alpha.interpolateTo(1, 0.2f);
    leftbar.position = Vector((leftbar.width - width) / 2, 0);
    leftbar.followCamera = 1;
    if(!leftbar.getParent())
    {
        leftbar.init();
        addChild(&leftbar, PM_STATIC);

        panels.resize(leftbar.icons.size());
        std::fill(panels.begin(), panels.end(), (IconGridPanel*)NULL);
    }

    rightbar.width = 100;
    rightbar.height = height;
    rightbar.alpha = 0;
    rightbar.alpha.interpolateTo(1, 0.2f);
    rightbar.position = Vector(((width - rightbar.width) / 2), 0);
    rightbar.followCamera = 1;
    if(!rightbar.getParent())
    {
        rightbar.init();
        addChild(&rightbar, PM_STATIC);
    }

    for(int i = 0; i < panels.size(); ++i)
    {
        if(panels[i])
            continue;
        panels[i] = new IconGridPanel();
        panels[i]->followCamera = 1;
        panels[i]->width = width - leftbar.width - rightbar.width;
        panels[i]->height = 750;
        panels[i]->position = Vector(0, 0);
        panels[i]->alpha = 0;
        panels[i]->spacing = 20; // for the grid
        panels[i]->scale = Vector(0.8f, 0.8f);
        leftbar.icons[i]->cb = _MenuIconClickCallback;
        leftbar.icons[i]->cb_data = this;
        addChild(panels[i], PM_POINTER);
    }

    // NEW GRID VIEW

    initModAndPatchPanel();
    // net panel inited on demand

    showPanel(0);

    // we abuse those
    dsq->subtext->alpha = 1.2f;
    dsq->subbox->alpha = 1;

    // TODO: keyboard/gamepad control
}

void ModSelectorScreen::initModAndPatchPanel()
{
    IconGridPanel *modgrid = panels[0];
    IconGridPanel *patchgrid = panels[1];
    ModIcon *ico;
    std::vector<ModIcon*> tv; // for sorting
    tv.resize(dsq->modEntries.size());
    for(unsigned int i = 0; i < tv.size(); ++i)
    {
        ico = NULL;
        for(RenderObject::Children::iterator it = modgrid->children.begin(); it != modgrid->children.end(); ++it)
            if(ModIcon* other = dynamic_cast<ModIcon*>(*it))
                if(other->modId == i)
                {
                    ico = other;
                    break;
                }

        if(!ico)
        {
            for(RenderObject::Children::iterator it = patchgrid->children.begin(); it != patchgrid->children.end(); ++it)
                if(ModIcon* other = dynamic_cast<ModIcon*>(*it))
                    if(other->modId == i)
                    {
                        ico = other;
                        break;
                    }

            if(!ico) // ok, its really not there.
            {
                ico = new ModIcon;
                ico->followCamera = 1;
                std::ostringstream os;
                os << "Created ModIcon " << i;
                debugLog(os.str());
            }
        }
            
        tv[i] = ico;
        ico->loadEntry(dsq->modEntries[i]);
    }
    std::sort(tv.begin(), tv.end(), _modname_cmp);

    for(int i = 0; i < tv.size(); ++i)
    {
        if(!tv[i]->getParent()) // ensure it was not added earlier
        {
            if(tv[i]->modType == MODTYPE_PATCH)
                patchgrid->add(tv[i]);
            else
                modgrid->add(tv[i]);
        }
    }
    updateFade();
}

void ModSelectorScreen::initNetPanel()
{
    if(!gotServerList)
    {
#ifndef AQUARIA_DEMO
        _FetchModList(dsq->user.network.masterServer, MAX_SERVER_HOPS);
#endif
        gotServerList = true; // try only once
    }

    updateFade();
}

static void _ShareAllAlpha(RenderObject *r)
{
    r->shareAlphaWithChildren = true;
    for(RenderObject::Children::iterator it = r->children.begin(); it != r->children.end(); ++it)
        _ShareAllAlpha(*it);
}

void ModSelectorScreen::close()
{
    for(int i = 0; i < panels.size(); ++i)
        if(i != currentPanel)
            panels[i]->setHidden(true);

    const float t = 0.5f;
    _ShareAllAlpha(this);
    //panels[currentPanel]->scale.interpolateTo(Vector(0.9f, 0.9f), t); // HMM
    dsq->subtext->alpha.interpolateTo(0, t/1.2f);
    dsq->subbox->alpha.interpolateTo(0, t);
    dsq->user.save();
}

JuicyProgressBar::JuicyProgressBar() : Quad(), txt(&dsq->smallFont)
{
    setTexture("modselect/tube");
    //shareAlphaWithChildren = true;
    followCamera = 1;
    alpha = 1;

    juice.setTexture("loading/juice");
    juice.alpha = 0.8;
    juice.followCamera = 1;
    addChild(&juice, PM_STATIC);

    txt.alpha = 0.7;
    txt.followCamera = 1;
    addChild(&txt, PM_STATIC);

    progress(0);
}

void JuicyProgressBar::progress(float p)
{
    juice.width = p * width;
    juice.height = height - 4;
    perc = p;
}

BasicIcon::BasicIcon() : AquariaGuiQuad(), mouseDown(false),
scaleNormal(1,1), scaleBig(scaleNormal * 1.1f)
{
}

void BasicIcon::onUpdate(float dt)
{
    AquariaGuiQuad::onUpdate(dt);

    if (isCoordinateInside(core->mouse.position))
    {
        scale.interpolateTo(scaleBig, 0.1f);
        const bool anyButton = core->mouse.buttons.left || core->mouse.buttons.right;
        if (anyButton && !mouseDown)
        {
            mouseDown = true;
        }
        else if (!anyButton && mouseDown)
        {
            if(alpha.x > 0.1f) // do not trigger if invis
                onClick();
            mouseDown = false;
        }
    }
    else
    {
        scale.interpolateTo(scaleNormal, 0.1f);
        mouseDown = false;
    }
}

void SubtitleIcon::onUpdate(float dt)
{
    BasicIcon::onUpdate(dt);

    if (alpha.x > 0.1f && isCoordinateInside(core->mouse.position))
        dsq->subtext->setText(label);
}

void BasicIcon::onClick()
{
    dsq->sound->playSfx("denied");
}

MenuIcon::MenuIcon(int id) : SubtitleIcon(), iconId(id), cb(0), cb_data(0)
{
}

void MenuIcon::onClick()
{
    dsq->sound->playSfx("click");
    if(cb)
        cb(iconId, cb_data);
}


ModIcon::ModIcon(): SubtitleIcon(), modId(-1)
{
}

void ModIcon::onClick()
{
    dsq->sound->playSfx("click");

#ifdef AQUARIA_DEMO
    dsq->nag(NAG_TOTITLE);
    return;
#endif

    switch(modType)
    {
        case MODTYPE_MOD:
        {
            dsq->sound->playSfx("pet-on");
            core->quitNestedMain();
            dsq->modIsSelected = true;
            dsq->selectedMod = modId;
            break;
        }

        case MODTYPE_PATCH:
        {
            std::set<std::string>::iterator it = dsq->activePatches.find(fname);
            if(it != dsq->activePatches.end())
            {
                dsq->sound->playSfx("pet-off");
                dsq->unapplyPatch(fname);
                dsq->screenMessage(modname + " - deactivated"); // DEBUG
            }
            else
            {
                dsq->sound->playSfx("pet-on");
                dsq->applyPatch(fname);
                dsq->screenMessage(modname + " - activated"); // DEBUG
            }
            updateStatus();
            break;
        }

        default:
            errorLog("void ModIcon::onClick() -- unknown modType");
    }
}

void ModIcon::loadEntry(const ModEntry& entry)
{
    modId = entry.id;
    modType = entry.type;
    fname = entry.path;

    std::string texToLoad = entry.path + "/" + "mod-icon";
#if defined(BBGE_BUILD_UNIX)
    texToLoad = dsq->getUserDataFolder() + "/_mods/" + texToLoad;
#else
    texToLoad = "./_mods/" + texToLoad;
#endif
    setTexture(texToLoad);
    width = height = MOD_ICON_SIZE;

    TiXmlDocument d;

    dsq->mod.loadModXML(&d, entry.path);

    label = "No Description";

    TiXmlElement *top = d.FirstChildElement("AquariaMod");
    if (top)
    {
        TiXmlElement *desc = top->FirstChildElement("Description");
        if (desc)
        {
            if (desc->Attribute("text"))
            {
                label = desc->Attribute("text");
                if (label.size() > 255)
                    label.resize(255);
            }
        }
        TiXmlElement *fullname = top->FirstChildElement("Fullname");
        if (fullname)
        {
            if (fullname->Attribute("text"))
            {
                modname = fullname->Attribute("text");
                if (modname.size() > 60)
                    modname.resize(60);
            }
        }
    }
    updateStatus();
}

void ModIcon::updateStatus()
{
    if(modType == MODTYPE_PATCH)
    {
        std::set<std::string>::iterator it = dsq->activePatches.find(fname);
        if(it != dsq->activePatches.end())
        {
            // enabled
            color.interpolateTo(Vector(1,1,1), 0.1f);
            alpha.interpolateTo(1, 0.2f);
            scaleNormal = Vector(1,1);
        }
        else
        {
            // disabled
            color.interpolateTo(Vector(0.5f, 0.5f, 0.5f), 0.1f);
            alpha.interpolateTo(0.6f, 0.2f);
            scaleNormal = Vector(0.8f,0.8f);
        }
        scaleBig = scaleNormal * 1.1f;
    }
}

ModIconOnline::ModIconOnline(const std::string& mod, const std::string& desc, const std::string& icon, const std::string& pkg)
: SubtitleIcon(), fname(mod), iconfile(icon), packageUrl(pkg), pb(0), extraIcon(0), clickable(true)
// FIXME clickable - need to dl image first? better not. but what else to do to prevent messing up the progressbar?
{
    label = desc;
    width = height = MOD_ICON_SIZE;
}

// return true if the desired texture could be set
bool ModIconOnline::fixIcon()
{
    if(dsq->vfs.GetFile(iconfile.c_str()))
    {
        setTexture(iconfile);
        width = height = MOD_ICON_SIZE;
        return Texture::textureError == TEXERR_OK;
    }
    if(!texture)
    {
        //setTexture("bitblot/logo");
        int i = (rand() % 7) + 1;
        std::stringstream ss;
        ss << "fish-000" << i;
        setTexture(ss.str());

        if(width > MOD_ICON_SIZE || height > MOD_ICON_SIZE)
            width = height = MOD_ICON_SIZE;
    }
    return false;
}

void ModIconOnline::onClick()
{
    dsq->sound->playSfx("click");

#ifdef AQUARIA_DEMO
    dsq->nag(NAG_TOTITLE);
    return;
#endif

    bool success = false;
    std::string plainName;

    if(clickable && _CreateLocalPackageName(packageUrl, plainName, localPackageName))
    {
        bool proceed = true;
        if(dsq->modIsKnown(plainName))
        {
            mouseDown = false; // HACK: do this here else stack overflow!
            proceed = dsq->confirm("Mod already exists. Re-download?");
        }

        if(proceed && confirmStr.length())
        {
            mouseDown = false; // HACK: do this here else stack overflow!
            dsq->sound->playSfx("spirit-beacon");
            proceed = dsq->confirm(confirmStr);
        }

        if(proceed)
        {
            if(Network::download(packageUrl, localPackageName, false, false, wget_notify, this, MODDL_PACKAGE, packageUrl.c_str()))
            {
                setDownloadProgress(0);
                success = true;
                clickable = false;
            }
        }
        else
            success = true; // we didn't want, anyway
    }

    if(!success)
    {
        SubtitleIcon::onClick(); // denied
        if(clickable)
        {
            mouseDown = false; // HACK: do this here else stack overflow!
            dsq->confirm("Unable to download file, bad URL", "", true);
        }
    }
}

void ModIconOnline::setDownloadProgress(float p, float barheight /* = 20 */)
{
    if(!pb)
    {
        pb = new JuicyProgressBar;
        addChild(pb, PM_POINTER);
        pb->width = width;
        pb->height = 0;
        pb->alpha = 0;
    }

    if(barheight != pb->height)
    {
        pb->height = barheight;
        pb->width = width;
        pb->position = Vector(0, (height - pb->height + 1) / 2); // +1 skips a pixel row and looks better
    }

    if(p >= 0 && p <= 1)
    {
        pb->alpha.interpolateTo(1, 0.2f);
        pb->progress(p);
    }
    else
    {
        pb->alpha.interpolateTo(0, 0.2f);
        pb->progress(0);
    }
}

void ModIconOnline::updateStatus()
{
    // TODO: update extra icon and stuff
}

MenuBasicBar::MenuBasicBar() : AquariaGuiQuad()
{
    setTexture("modselect/bar");
    repeatTextureToFill(true);
    shareAlphaWithChildren = true;
}

void MenuBasicBar::init()
{
}

void MenuIconBar::init()
{
    MenuIcon *ico;
    int y = -height / 2;

    ico = new MenuIcon(0);
    ico->label = "Browse installed mods";
    ico->setTexture("modselect/hdd");
    y += ico->height;
    ico->position = Vector(0, y);
    add(ico);

    ico = new MenuIcon(1);
    ico->label = "Browse & enable/disable installed patches";
    ico->setTexture("modselect/patch");
    y += ico->height;
    ico->position = Vector(0, y);
    add(ico);

    ico = new MenuIcon(2);
    ico->label = "Browse mods online";
    ico->setTexture("modselect/globe");
    y += ico->height;
    ico->position = Vector(0, y);
    add(ico);

    ico = new MenuIcon(3);
    ico->label = "Return to title";
    ico->setTexture("gui/wok-drop");
    ico->repeatTextureToFill(false);
    y += ico->height;
    ico->position = Vector(0, y);
    add(ico);
}

void MenuIconBar::add(MenuIcon *ico)
{
    ico->width = ico->height = width;
    ico->followCamera = 1;
    icons.push_back(ico);
    addChild(ico, PM_POINTER);
}

void MenuArrowBar::init()
{
    // TODO: up/down arrow
}

IconGridPanel::IconGridPanel() : AquariaGuiQuad(), spacing(0), y(0), x(0)
{
    shareAlphaWithChildren = false; // patch selection icons need their own alpha, use fade() instead
    alphaMod = 0.01f;
    color = 0;
}

void IconGridPanel::add(RenderObject *obj)
{
    const int xoffs = (-width / 2) + (obj->width / 2) + spacing;
    const int yoffs = (-height / 2) + obj->height + spacing;
    const int xlim = width - obj->width;
    Vector newpos;

    if(x >= xlim)
    {
        x = 0;
        y += (obj->height + spacing);
    }

    newpos = Vector(x + xoffs, y + yoffs);
    x += (obj->width + spacing);

    obj->position = newpos;
    addChild(obj, PM_POINTER);
}

void IconGridPanel::fade(bool in, bool sc)
{
    const float t = 0.2f;
    Vector newalpha;
    if(in)
    {
        newalpha.x = 1;
        if(sc)
            scale.interpolateTo(Vector(1, 1), t);
    }
    else
    {
        newalpha.x = 0;
        if(sc)
            scale.interpolateTo(Vector(0.8f, 0.8f), t);
    }
    alpha.interpolateTo(newalpha, t);

    for(Children::iterator it = children.begin(); it != children.end(); ++it)
    {
        (*it)->alpha.interpolateTo(newalpha, t);
        
        if(in)
            if(ModIcon *ico = dynamic_cast<ModIcon*>(*it))
                ico->updateStatus();
    }
}


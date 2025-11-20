import os
import os.path
import shutil

basefolder = os.path.dirname(os.path.abspath(__file__))

def inject_gdf(folder, name, langfold, langid):
    exename = os.path.join(basefolder, folder, name+'.exe')
    gdfname = os.path.join(basefolder, folder, 'GDF', langfold, name+'.gdf.xml')
    rv = os.system('C:/Cryptic/tools/bin/resourceinfo.exe -updateresource "%s" DATA __GDF_XML %s "%s"'%(exename, langid, gdfname))
    assert(rv == 0)

def inject_thumb(folder, name):
    exename = os.path.join(basefolder, folder, name+'.exe')
    imgname = os.path.join(basefolder, folder, folder+'_thumb.png')
    if os.path.exists(imgname):
        rv = os.system('C:/Cryptic/tools/bin/resourceinfo.exe -updateresource "%s" DATA __GDF_THUMBNAIL 1024 "%s"'%(exename, imgname))
        assert(rv == 0)

def create_game(folder, name):
    exename = os.path.join(basefolder, folder, name+'.exe')
    shutil.copyfile('../bin/CrypticLauncher.exe', exename)

    # Inject the game icon
    iconname = os.path.join(basefolder, folder, folder+'.ico')
    rv = os.system('C:/Cryptic/tools/bin/ReplaceVistaIcon.exe "%s" "%s" 114'%(exename, iconname))
    assert(rv == 0)
    #C:\Cryptic\tools\bin\ReplaceVistaIcon.exe ..\bin\CrypticLauncher.exe crypticBig.ico 114

    # Inject the GDFs
    inject_gdf(folder, name, 'NEU', 1024)
    inject_gdf(folder, name, 'ENU', 1033)
    inject_gdf(folder, name, 'FRA', 1036)
    inject_gdf(folder, name, 'DEU', 1031)
    
    # Inject the GDF thumbnail
    inject_thumb(folder, name)
    
    # Sign the new launcher
    os.system('C:/src/3rdparty/authenticode/signtool.exe sign /f "..\\..\\3rdparty\\authenticode\\cryptic.pfx" /p "cryptic" /d "%s Launcher" /du "http://www.crypticstudios.com/" /t http://timestamp.verisign.com/scripts/timstamp.dll "%s"'%(name, exename))

if __name__ == '__main__':
    create_game('FC', 'Champions Online')
    create_game('STO', 'Star Trek Online')
    create_game('NNO', 'Neverwinter')
    create_game('CN', 'Creatures of the Night')
    create_game('BA', 'Bronze Age')
    #os.system('resourceinfo -resource DATA __GDF_XML 1024 "..\CrypticLauncher\games\FC\Champions Online.exe"')
    #os.chdir('C:/Program Files (x86)/Microsoft DirectX SDK (March 2009)/Utilities/bin/x86')
    #os.system('GDFTrace.exe "C:\src\core\CrypticLauncher\games\FC\Champions Online.exe"')
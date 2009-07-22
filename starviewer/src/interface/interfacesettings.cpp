#include "interfacesettings.h"
#include "settingsregistry.h"


#include <QDir>

namespace udg {

// Definició de les claus
const QString openBaseKey("Starviewer-App-ImportFile/");
const QString InterfaceSettings::openFileLastPathKey( openBaseKey + "workingDirectory" );
const QString InterfaceSettings::openDirectoryLastPathKey( openBaseKey + "workingDicomDirectory" );
const QString InterfaceSettings::openFileLastFileExtensionKey( openBaseKey + "defaultExtension" );

const QString InterfaceSettings::applicationMainWindowGeometryKey("geometry");
const QString InterfaceSettings::allowMultipleInstancesPerExtensionKey("Extensions/allowMultipleExtensionInstances");

InterfaceSettings::InterfaceSettings()
{
}

InterfaceSettings::~InterfaceSettings()
{
}

void InterfaceSettings::init()
{
    SettingsRegistry *settingsRegistry = SettingsRegistry::instance();
    settingsRegistry->addSetting( openFileLastPathKey, QDir::homePath() );
    settingsRegistry->addSetting( openDirectoryLastPathKey, QDir::homePath()  );
    settingsRegistry->addSetting( openFileLastFileExtensionKey, "MetaIO Image (*.mhd)" );
    settingsRegistry->addSetting( allowMultipleInstancesPerExtensionKey, false );
}

} // end namespace udg 


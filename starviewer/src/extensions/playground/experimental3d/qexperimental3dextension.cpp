#include "qexperimental3dextension.h"

#include <vtkImageCast.h>
#include <vtkImageData.h>
#include <vtkImageGaussianSmooth.h>
#include <vtkImageMathematics.h>

#include "experimental3dvolume.h"
#include "informationtheory.h"
#include "logging.h"
#include "mathtools.h"
#include "obscurancemainthread.h"
#include "transferfunctionio.h"
#include "vector3.h"
#include "viewpointgenerator.h"
#include "viewpointinformationchannel.h"
#include "viewpointintensityinformationchannel.h"
#include "volumereslicer.h"
#include "experimental3dsettings.h"

#include <QButtonGroup>
#include <QColorDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QStringListModel>
#include <QTextStream>
#include <QTime>
#include <QtCore/qmath.h>

#ifdef CUDA_AVAILABLE
#include "cudafiltering.h"
#endif


namespace udg {


QExperimental3DExtension::QExperimental3DExtension( QWidget *parent )
 : QWidget( parent ), m_volume( 0 ),
   m_computingObscurance( false ), m_obscuranceMainThread( 0 ), m_obscurance( 0 ), m_interactive( true )
{
    setupUi( this );
    Experimental3DSettings().init();

    createConnections();

    QButtonGroup *bestViewpointsRadioButtons = new QButtonGroup( this );
    bestViewpointsRadioButtons->addButton( m_computeBestViewsNRadioButton );
    bestViewpointsRadioButtons->addButton( m_computeBestViewsThresholdRadioButton );

    m_colorVomiPalette << Vector3Float( 1.0f, 1.0f, 1.0f );

    m_recentTransferFunctionsModel = new QStringListModel( this );
    m_recentTransferFunctionsListView->setModel( m_recentTransferFunctionsModel );

#ifndef CUDA_AVAILABLE
    m_vmiDisplayCheckBox->setChecked( true );
    m_vmiDisplayCheckBox->setEnabled( false );
#endif // CUDA_AVAILABLE
}


QExperimental3DExtension::~QExperimental3DExtension()
{
    delete m_volume;

    if ( m_computingObscurance )
    {
        m_obscuranceMainThread->stop();
        m_obscuranceMainThread->wait();
    }

    delete m_obscuranceMainThread;
    delete m_obscurance;
}


void QExperimental3DExtension::setInput( Volume *input )
{
    m_volume = new Experimental3DVolume( input );

    m_viewer->setInput( input );
    m_viewer->setVolume( m_volume );

    unsigned short max = m_volume->getRangeMax();
    m_transferFunctionEditor->setRange(0, max);
    m_transferFunctionEditor->syncToMax();

    TransferFunction defaultTransferFunction;
    defaultTransferFunction.addPoint( 0, QColor( 0, 0, 0, 0 ) );
    defaultTransferFunction.addPoint( max, QColor( 255, 255, 255, 255 ) );
    m_transferFunctionEditor->setTransferFunction( defaultTransferFunction );

    setTransferFunction( false );
    render();
}


void QExperimental3DExtension::setNewVolume(Volume *volume)
{
    m_viewer->removeCurrentVolume();
    delete m_volume;

    setInput(volume);
}


void QExperimental3DExtension::saveViewedVolume(QString fileName)
{
    saveData(fileName, Experimental3DSettings::ViewedVolumeDir, tr("viewed volume"), FileExtensionsTxtAll, "txt", m_viewedVolume, "volume(v%1) = %2", 1);
}


void QExperimental3DExtension::loadHV(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::HVDir, tr("viewpoints entropy H(V)"), FileExtensionsDatAll, m_HV))
    {
        m_saveHVPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveHV(QString fileName)
{
    saveData(fileName, Experimental3DSettings::HVDir, tr("viewpoints entropy H(V)"), FileExtensionsTxtDatAll, "txt", m_HV, "H(V) = %1");
}


void QExperimental3DExtension::loadHVz(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::HVzDir, tr("H(V|z)"), FileExtensionsDatAll, m_HVz))
    {
        m_saveHVzPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveHVz(QString fileName)
{
    saveData(fileName, Experimental3DSettings::HVzDir, tr("H(V|z)"), FileExtensionsDatAll, "dat", m_HVz);
}


void QExperimental3DExtension::loadHZ(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::HZDir, tr("voxels entropy H(Z)"), FileExtensionsDatAll, m_HZ))
    {
        m_saveHZPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveHZ(QString fileName)
{
    saveData(fileName, Experimental3DSettings::HZDir, tr("voxels entropy H(Z)"), FileExtensionsTxtDatAll, "txt", m_HZ, "H(Z) = %1");
}


void QExperimental3DExtension::loadHZv(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::HZvDir, tr("H(Z|v)"), FileExtensionsDatAll, m_HZv))
    {
        m_saveHZvPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveHZv(QString fileName)
{
    saveData(fileName, Experimental3DSettings::HZvDir, tr("H(Z|v)"), FileExtensionsTxtDatAll, "txt", m_HZv, "H(Z|v%1) = %2", 1);
}


void QExperimental3DExtension::loadHZV(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::HZVDir, tr("H(Z|V)"), FileExtensionsDatAll, m_HZV))
    {
        m_saveHZVPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveHZV(QString fileName)
{
    saveData(fileName, Experimental3DSettings::HZVDir, tr("H(Z|V)"), FileExtensionsTxtDatAll, "txt", m_HZV, "H(Z|V) = %1");
}


void QExperimental3DExtension::loadVmi(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::ViewpointMutualInformationDir, tr("VMI I₁(v;Z)"), FileExtensionsDatAll, m_vmi))
    {
        m_saveVmiPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveVmi(QString fileName)
{
    saveData(fileName, Experimental3DSettings::ViewpointMutualInformationDir, tr("VMI I₁(v;Z)"), FileExtensionsTxtDatAll, "txt", m_vmi, "VMI(v%1) = %2", 1);
}


void QExperimental3DExtension::loadVmi2(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::ViewpointMutualInformation2Dir, tr("VMI2 I₂(v;Z)"), FileExtensionsDatAll, m_vmi2))
    {
        m_saveVmi2PushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveVmi2(QString fileName)
{
    saveData(fileName, Experimental3DSettings::ViewpointMutualInformation2Dir, tr("VMI2 I₂(v;Z)"), FileExtensionsTxtDatAll, "txt", m_vmi2, "VMI2(v%1) = %2", 1);
}


void QExperimental3DExtension::loadVmi3(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::ViewpointMutualInformation3Dir, tr("VMI3 I₃(v;Z)"), FileExtensionsDatAll, m_vmi3))
    {
        m_saveVmi3PushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveVmi3(QString fileName)
{
    saveData(fileName, Experimental3DSettings::ViewpointMutualInformation3Dir, tr("VMI3 I₃(v;Z)"), FileExtensionsTxtDatAll, "txt", m_vmi3, "VMI3(v%1) = %2", 1);
}


void QExperimental3DExtension::loadMi(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::MutualInformationDir, tr("mutual information I(V;Z)"), FileExtensionsDatAll, m_mi))
    {
        m_saveMiPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveMi( QString fileName )
{
    saveData(fileName, Experimental3DSettings::MutualInformationDir, tr("mutual information I(V;Z)"), FileExtensionsTxtDatAll, "txt", m_mi, "I(V;Z) = %1");
}


void QExperimental3DExtension::loadViewpointUnstabilities(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::ViewpointUnstabilitiesDir, tr("viewpoint unstabilities"), FileExtensionsDatAll, m_viewpointUnstabilities))
    {
        m_saveViewpointUnstabilitiesPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveViewpointUnstabilities(QString fileName)
{
    saveData(fileName, Experimental3DSettings::ViewpointUnstabilitiesDir, tr("viewpoint unstabilities"), FileExtensionsTxtDatAll, "txt", m_viewpointUnstabilities, "U(v%1) = %2", 1);
}


void QExperimental3DExtension::loadVomi(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::VoxelMutualInformationDir, tr("VoMI I₁(z;V)"), FileExtensionsDatAll, m_vomi))
    {
        int nVoxels = m_vomi.size();
        m_maximumVomi = 0.0f;

        for (int j = 0; j < nVoxels; j++) if (m_vomi.at(j) > m_maximumVomi) m_maximumVomi = m_vomi.at(j);

        m_baseVomiRadioButton->setEnabled(true);
        m_vomiCheckBox->setEnabled(true);
        m_vomiCoolWarmCheckBox->setEnabled(true);
        m_opacityLabel->setEnabled(true);
        m_opacityVomiCheckBox->setEnabled(true);
        m_saveVomiPushButton->setEnabled(true);
        m_vomiGradientPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveVomi(QString fileName)
{
    saveData(fileName, Experimental3DSettings::VoxelMutualInformationDir, tr("VoMI I₁(z;V)"), FileExtensionsDatAll, "dat", m_vomi);
}


void QExperimental3DExtension::loadVomi2(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::VoxelMutualInformation2Dir, tr("VoMI2 I₂(z;V)"), FileExtensionsDatAll, m_vomi2))
    {
        int nVoxels = m_vomi2.size();
        m_maximumVomi2 = 0.0f;

        for (int j = 0; j < nVoxels; j++) if (m_vomi2.at(j) > m_maximumVomi2) m_maximumVomi2 = m_vomi2.at(j);

        m_baseVomiRadioButton->setEnabled(true);
        m_vomiCheckBox->setEnabled(true);
        //m_vomiCoolWarmCheckBox->setEnabled(true);   /// \todo vomi2
        //m_opacityLabel->setEnabled(true);
        //m_opacityVomiCheckBox->setEnabled(true);    /// \todo vomi2
        m_saveVomi2PushButton->setEnabled(true);
        m_vomi2GradientPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveVomi2(QString fileName)
{
    saveData(fileName, Experimental3DSettings::VoxelMutualInformation2Dir, tr("VoMI2 I₂(z;V)"), FileExtensionsDatAll, "dat", m_vomi2);
}


void QExperimental3DExtension::loadVomi3(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::VoxelMutualInformation3Dir, tr("VoMI3 I₃(z;V)"), FileExtensionsDatAll, m_vomi3))
    {
        int nVoxels = m_vomi3.size();
        m_maximumVomi3 = 0.0f;

        for (int j = 0; j < nVoxels; j++) if (m_vomi3.at(j) > m_maximumVomi3) m_maximumVomi3 = m_vomi3.at(j);

        m_baseVomiRadioButton->setEnabled(true);
        m_vomiCheckBox->setEnabled(true);
        //m_vomiCoolWarmCheckBox->setEnabled(true);   /// \todo vomi3
        //m_opacityLabel->setEnabled(true);
        //m_opacityVomiCheckBox->setEnabled(true);    /// \todo vomi3
        m_saveVomi3PushButton->setEnabled(true);
        //m_vomiGradientPushButton->setEnabled(true); /// \todo vomi3
    }
}


void QExperimental3DExtension::saveVomi3(QString fileName)
{
    saveData(fileName, Experimental3DSettings::VoxelMutualInformation3Dir, tr("VoMI3 I₃(z;V)"), FileExtensionsDatAll, "dat", m_vomi3);
}


void QExperimental3DExtension::loadViewpointVomi(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::ViewpointVoxelMutualInformationDir, tr("viewpoint VoMI (INF)"), FileExtensionsDatAll, m_viewpointVomi))
    {
        m_saveViewpointVomiPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveViewpointVomi(QString fileName)
{
    saveData(fileName, Experimental3DSettings::ViewpointVoxelMutualInformationDir, tr("viewpoint VoMI (INF)"), FileExtensionsTxtDatAll, "txt", m_viewpointVomi, "VVoMI(v%1) = %2", 1);
}


void QExperimental3DExtension::loadViewpointVomi2(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::ViewpointVoxelMutualInformation2Dir, tr("viewpoint VoMI2 (INF2)"), FileExtensionsDatAll, m_viewpointVomi2))
    {
        m_saveViewpointVomi2PushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveViewpointVomi2(QString fileName)
{
    saveData(fileName, Experimental3DSettings::ViewpointVoxelMutualInformation2Dir, tr("viewpoint VoMI2 (INF2)"), FileExtensionsTxtDatAll, "txt", m_viewpointVomi2, "VVoMI2(v%1) = %2", 1);
}


void QExperimental3DExtension::loadColorVomiPalette( QString fileName )
{
    if ( fileName.isEmpty() )
    {
        fileName = getFileNameToLoad( Experimental3DSettings::ColorVoxelMutualInformationPaletteDir, tr("Load color VoMI palette"), tr("Text files (*.txt);;All files (*)") );
        if ( fileName.isNull() ) return;
    }

    QFile colorVomiPaletteFile( fileName );

    if ( !colorVomiPaletteFile.open( QFile::ReadOnly | QFile::Text ) )
    {
        DEBUG_LOG( QString( "No es pot llegir el fitxer " ) + fileName );
        if ( m_interactive ) QMessageBox::warning( this, tr("Can't load color VoMI palette"), QString( tr("Can't load color VoMI palette from file ") ) + fileName );
        return;
    }

    m_colorVomiPalette.clear();

    QTextStream in( &colorVomiPaletteFile );

    while ( !in.atEnd() )
    {
        QString line = in.readLine();
        QStringList numbers = line.split( ' ', QString::SkipEmptyParts );

        if ( numbers.size() < 3 ) continue;

        Vector3Float color;

        if ( numbers.at( 0 ).contains( '.' ) )  // reals [0,1]
        {
            color.x = numbers.at( 0 ).toFloat();
            color.y = numbers.at( 1 ).toFloat();
            color.z = numbers.at( 2 ).toFloat();
        }
        else    // enters [0,255]
        {
            color.x = static_cast<unsigned char>( numbers.at( 0 ).toUShort() ) / 255.0f;
            color.y = static_cast<unsigned char>( numbers.at( 1 ).toUShort() ) / 255.0f;
            color.z = static_cast<unsigned char>( numbers.at( 2 ).toUShort() ) / 255.0f;
        }

        m_colorVomiPalette << color;
    }

    if ( m_colorVomiPalette.isEmpty() ) m_colorVomiPalette << Vector3Float( 1.0f, 1.0f, 1.0f );

    colorVomiPaletteFile.close();
}


void QExperimental3DExtension::loadColorVomi(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::ColorVoxelMutualInformationDir, tr("color VoMI"), FileExtensionsDatAll, m_colorVomi))
    {
        int nVoxels = m_colorVomi.size();
        m_maximumColorVomi = 0.0f;

        for (int j = 0; j < nVoxels; j++)
        {
            const Vector3Float &colorVomi = m_colorVomi.at(j);
            if (colorVomi.x > m_maximumColorVomi) m_maximumColorVomi = colorVomi.x;
            if (colorVomi.y > m_maximumColorVomi) m_maximumColorVomi = colorVomi.y;
            if (colorVomi.z > m_maximumColorVomi) m_maximumColorVomi = colorVomi.z;
        }

        m_colorVomiCheckBox->setEnabled(true);
        m_saveColorVomiPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveColorVomi(QString fileName)
{
    saveData(fileName, Experimental3DSettings::ColorVoxelMutualInformationDir, tr("color VoMI"), FileExtensionsDatAll, "dat", m_colorVomi);
}


void QExperimental3DExtension::loadEvmiOpacityOtherTransferFunction( QString fileName )
{
    if ( !m_computeEvmiOpacityUseOtherPushButton->isChecked() ) return;

    if ( fileName.isEmpty() )
    {
        fileName = getFileNameToLoad( Experimental3DSettings::TransferFunctionDir, tr("Load transfer function"), tr("XML files (*.xml);;Transfer function files (*.tf);;All files (*)") );
        if ( fileName.isNull() ) return;
    }

    TransferFunction *transferFunction;

    if ( fileName.endsWith( ".xml" ) ) transferFunction = TransferFunctionIO::fromXmlFile( fileName );
    else transferFunction = TransferFunctionIO::fromFile( fileName );

    m_evmiOpacityTransferFunction = *transferFunction;

    delete transferFunction;
}


void QExperimental3DExtension::loadEvmiOpacity(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::EnhancedViewpointMutualInformationOpacityDir, tr("EVMI with opacity"), FileExtensionsDatAll, m_evmiOpacity))
    {
        m_saveEvmiOpacityPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveEvmiOpacity(QString fileName)
{
    saveData(fileName, Experimental3DSettings::EnhancedViewpointMutualInformationOpacityDir, tr("EVMI with opacity"), FileExtensionsTxtDatAll, "txt", m_evmiOpacity, "EVMI(v%1) = %2", 1);
}


void QExperimental3DExtension::loadEvmiVomi(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::EnhancedViewpointMutualInformationVoxelMutualInformationDir, tr("EVMI with VoMI"), FileExtensionsDatAll, m_evmiVomi))
    {
        m_saveEvmiVomiPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveEvmiVomi(QString fileName)
{
    saveData(fileName, Experimental3DSettings::EnhancedViewpointMutualInformationVoxelMutualInformationDir, tr("EVMI with VoMI"), FileExtensionsTxtDatAll, "txt", m_evmiVomi, "EVMI(v%1) = %2", 1);
}


void QExperimental3DExtension::loadBestViews( QString fileName )
{
    if ( fileName.isEmpty() )
    {
        fileName = getFileNameToLoad( Experimental3DSettings::BestViewsDir, tr("Load best views"), tr("Data files (*.dat);;All files (*)") );
        if ( fileName.isNull() ) return;
    }

    if ( loadData( fileName, m_bestViews ) )
    {
        m_saveBestViewsPushButton->setEnabled( true );
        m_tourBestViewsPushButton->setEnabled( true );
    }
    else if ( m_interactive ) QMessageBox::warning( this, tr("Can't load best views"), QString( tr("Can't load best views from file ") ) + fileName );
}


void QExperimental3DExtension::saveBestViews( QString fileName )
{
    if ( fileName.isEmpty() )
    {
        fileName = getFileNameToSave( Experimental3DSettings::BestViewsDir, tr("Save best views"), tr("Data files (*.dat);;Text files (*.txt);;All files (*)"), "dat" );
        if ( fileName.isNull() ) return;
    }

    bool error;

    if ( fileName.endsWith( ".txt" ) ) error = !saveDataAsText( m_bestViews, fileName, QString( "%1: v%2 %3" ), 0, 1 );
    else error = !saveData( m_bestViews, fileName );

    if ( error && m_interactive ) QMessageBox::warning( this, tr("Can't save best views"), QString( tr("Can't save best views to file ") ) + fileName );
}


void QExperimental3DExtension::loadGuidedTour( QString fileName )
{
    if ( fileName.isEmpty() )
    {
        fileName = getFileNameToLoad( Experimental3DSettings::GuidedTourDir, tr("Load guided tour"), tr("Data files (*.dat);;All files (*)") );
        if ( fileName.isNull() ) return;
    }

    if ( loadData( fileName, m_guidedTour ) )
    {
        m_saveGuidedTourPushButton->setEnabled( true );
        m_guidedTourPushButton->setEnabled( true );
    }
    else if ( m_interactive ) QMessageBox::warning( this, tr("Can't load guided tour"), QString( tr("Can't load guided tour from file ") ) + fileName );
}


void QExperimental3DExtension::saveGuidedTour( QString fileName )
{
    if ( fileName.isEmpty() )
    {
        fileName = getFileNameToSave( Experimental3DSettings::GuidedTourDir, tr("Save guided tour"), tr("Data files (*.dat);;Text files (*.txt);;All files (*)"), "dat" );
        if ( fileName.isNull() ) return;
    }

    bool error;

    if ( fileName.endsWith( ".txt" ) ) error = !saveDataAsText( m_guidedTour, fileName, QString( "%1: v%2 %3" ), 0, 1 );
    else error = !saveData( m_guidedTour, fileName );

    if ( error && m_interactive )QMessageBox::warning( this, tr("Can't save guided tour"), QString( tr("Can't save guided tour to file ") ) + fileName );
}


void QExperimental3DExtension::loadExploratoryTour( QString fileName )
{
    if ( fileName.isEmpty() )
    {
        fileName = getFileNameToLoad( Experimental3DSettings::ExploratoryTourDir, tr("Load exploratory tour"), tr("Data files (*.dat);;All files (*)") );
        if ( fileName.isNull() ) return;
    }

    if ( loadData( fileName, m_exploratoryTour ) )
    {
        m_saveExploratoryTourPushButton->setEnabled( true );
        m_exploratoryTourPushButton->setEnabled( true );
    }
    else if ( m_interactive ) QMessageBox::warning( this, tr("Can't load exploratory tour"), QString( tr("Can't load exploratory tour from file ") ) + fileName );
}


void QExperimental3DExtension::saveExploratoryTour( QString fileName )
{
    if ( fileName.isEmpty() )
    {
        fileName = getFileNameToSave( Experimental3DSettings::ExploratoryTourDir, tr("Save exploratory tour"), tr("Data files (*.dat);;Text files (*.txt);;All files (*)"), "dat" );
        if ( fileName.isNull() ) return;
    }

    bool error;

    if ( fileName.endsWith( ".txt" ) ) error = !saveDataAsText( m_exploratoryTour, fileName, QString( "%1: v%2 %3" ), 0, 1 );
    else error = !saveData( m_exploratoryTour, fileName );

    if ( error && m_interactive )QMessageBox::warning( this, tr("Can't save exploratory tour"), QString( tr("Can't save exploratory tour to file ") ) + fileName );
}


void QExperimental3DExtension::saveViewedVolumeI(QString fileName)
{
    saveData(fileName, Experimental3DSettings::ViewedVolumeIntensityDir, tr("viewed volume"), FileExtensionsTxtAll, "txt", m_viewedVolumeI, "volume(v%1) = %2", 1);
}


void QExperimental3DExtension::loadHI(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::HIDir, tr("intensities entropy H(I)"), FileExtensionsDatAll, m_HI))
    {
        m_saveHIPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveHI(QString fileName)
{
    saveData(fileName, Experimental3DSettings::HIDir, tr("intensities entropy H(I)"), FileExtensionsTxtDatAll, "txt", m_HI, "H(I) = %1");
}


void QExperimental3DExtension::loadHIv(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::HIvDir, tr("H(I|v)"), FileExtensionsDatAll, m_HIv))
    {
        m_saveHIvPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveHIv(QString fileName)
{
    saveData(fileName, Experimental3DSettings::HIvDir, tr("H(I|v)"), FileExtensionsTxtDatAll, "txt", m_HIv, "H(I|v%1) = %2", 1);
}


void QExperimental3DExtension::loadHIV(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::HIVDir, tr("H(I|V)"), FileExtensionsDatAll, m_HIV))
    {
        m_saveHZVPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveHIV(QString fileName)
{
    saveData(fileName, Experimental3DSettings::HIVDir, tr("H(I|V)"), FileExtensionsTxtDatAll, "txt", m_HIV, "H(I|V) = %1");
}


void QExperimental3DExtension::loadVmii(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::ViewpointMutualInformationIntensityDir, tr("VMIi"), FileExtensionsDatAll, m_vmii))
    {
        m_saveVmiiPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveVmii(QString fileName)
{
    saveData(fileName, Experimental3DSettings::ViewpointMutualInformationIntensityDir, tr("VMIi"), FileExtensionsTxtDatAll, "txt", m_vmii, "VMIi(v%1) = %2", 1);
}


void QExperimental3DExtension::loadMii(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::MutualInformationIntensityDir, tr("MIi"), FileExtensionsDatAll, m_mii))
    {
        m_saveMiiPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveMii(QString fileName)
{
    saveData(fileName, Experimental3DSettings::MutualInformationIntensityDir, tr("MIi"), FileExtensionsTxtDatAll, "txt", m_mii, "I(V;I) = %1");
}


void QExperimental3DExtension::loadViewpointUnstabilitiesI(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::ViewpointUnstabilitiesIntensityDir, tr("viewpoint unstabilities"), FileExtensionsDatAll, m_viewpointUnstabilitiesI))
    {
        m_saveViewpointUnstabilitiesIPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveViewpointUnstabilitiesI(QString fileName)
{
    saveData(fileName, Experimental3DSettings::ViewpointUnstabilitiesIntensityDir, tr("viewpoint unstabilities"), FileExtensionsTxtDatAll, "txt", m_viewpointUnstabilitiesI, "U(v%1) = %2", 1);
}


void QExperimental3DExtension::loadImi(QString fileName)
{
    if (loadData(fileName, Experimental3DSettings::IntensityMutualInformationDir, tr("IMI"), FileExtensionsDatAll, m_imi))
    {
        int nIntensities = m_imi.size();
        m_maximumImi = 0.0f;

        for ( int j = 0; j < nIntensities; j++ ) if ( m_imi.at( j ) > m_maximumImi ) m_maximumImi = m_imi.at( j );

        m_baseImiRadioButton->setEnabled(true);
//        m_baseImiCoolWarmRadioButton->setEnabled(true);
//        m_imiCheckBox->setEnabled(true);
//        m_imiCoolWarmCheckBox->setEnabled(true);
//        m_opacityLabel->setEnabled(true);
//        m_opacityImiCheckBox->setEnabled(true);
        m_saveImiPushButton->setEnabled(true);
//        m_imiGradientPushButton->setEnabled(true);
        m_colorTransferFunctionFromImiPushButton->setEnabled(true);
        m_opacityTransferFunctionFromImiPushButton->setEnabled(true);
        m_transferFunctionFromImiPushButton->setEnabled(true);
    }
}


void QExperimental3DExtension::saveImi(QString fileName)
{
    saveData(fileName, Experimental3DSettings::IntensityMutualInformationDir, tr("IMI"), FileExtensionsDatAll, "dat", m_imi);
}


const QString QExperimental3DExtension::FileExtensionsDatAll(tr("Data files (*.dat);;All files (*)"));
const QString QExperimental3DExtension::FileExtensionsTxtAll(tr("Text files (*.txt);;All files (*)"));
const QString QExperimental3DExtension::FileExtensionsTxtDatAll(tr("Text files (*.txt);;Data files (*.dat);;All files (*)"));


template <class T>
bool QExperimental3DExtension::loadData(QString &fileName, const QString &setting, const QString &name, const QString &extensions, T &data)
{
    if (fileName.isEmpty())
    {
        fileName = getFileNameToLoad(setting, QString(tr("Load %1")).arg(name), extensions);
        if (fileName.isNull()) return false;
    }

    QFile file(fileName);

    if (!file.open(QIODevice::ReadOnly))
    {
        DEBUG_LOG(QString("No es pot llegir el fitxer ") + fileName + ": " + file.errorString());
        if (m_interactive) QMessageBox::warning(this, QString(tr("Can't load %1 from file ")).arg(name).arg(fileName), file.errorString());
        return false;
    }

    QDataStream in(&file);
    loadData(in, data);

    file.close();

    return true;
}


template <class T>
void QExperimental3DExtension::loadData(QDataStream &in, T &data)
{
    if (!in.atEnd()) in >> data;
}


template <class T>
void QExperimental3DExtension::loadData(QDataStream &in, QVector<T> &data)
{
    data.clear();
    data.reserve(in.device()->size() / sizeof(T));

    while (!in.atEnd())
    {
        T datum;
        in >> datum;
        data << datum;
    }
}


template <class T>
bool QExperimental3DExtension::saveData(QString &fileName, const QString &setting, const QString &name, const QString &extensions, const QString &defaultSuffix, const T &data, const QString &textFormat, int base)
{
    if (fileName.isEmpty())
    {
        fileName = getFileNameToSave(setting, QString(tr("Save %1")).arg(name), extensions, defaultSuffix);
        if (fileName.isNull()) return false;
    }

    bool text = fileName.endsWith(".txt");
    QFile file(fileName);
    QIODevice::OpenMode mode = QIODevice::WriteOnly | QIODevice::Truncate;
    if (text) mode |= QIODevice::Text;

    if (!file.open(mode))
    {
        DEBUG_LOG(QString("No es pot escriure al fitxer ") + fileName + ": " + file.errorString());
        if (m_interactive) QMessageBox::warning(this, QString(tr("Can't save %1 to file ")).arg(name).arg(fileName), file.errorString());
        return false;
    }

    if (text)
    {
        QTextStream out(&file);
        saveData(out, data, textFormat, base);
    }
    else
    {
        QDataStream out(&file);
        saveData(out, data);
    }

    file.close();

    return true;
}


template <class T>
void QExperimental3DExtension::saveData(QDataStream &out, const T &data)
{
    out << data;
}


template <class T>
void QExperimental3DExtension::saveData(QTextStream &out, const T &data, const QString &textFormat, int base)
{
    Q_UNUSED(base);
    out << textFormat.arg(data) << "\n";
}


template <class T>
void QExperimental3DExtension::saveData(QDataStream &out, const QVector<T> &data)
{
    int n = data.size();
    for (int i = 0; i < n; i++) out << data.at(i);
}


template <class T>
void QExperimental3DExtension::saveData(QTextStream &out, const QVector<T> &data, const QString &textFormat, int base)
{
    int n = data.size();
    for (int i = 0; i < n; i++) out << textFormat.arg(i + base).arg(data.at(i)) << "\n";
}


template <class T>
bool QExperimental3DExtension::loadData( const QString &fileName, QList<T> &list )
{
    QFile file( fileName );

    if ( !file.open( QIODevice::ReadOnly ) )
    {
        DEBUG_LOG( QString( "No es pot llegir el fitxer " ) + fileName );
        return false;
    }

    list.clear();

    QDataStream in( &file );

    while ( !in.atEnd() )
    {
        T data;
        in >> data;
        list << data;
    }

    file.close();

    return true;
}


template <class T>
bool QExperimental3DExtension::saveData( const QList<T> &list, const QString &fileName )
{
    QFile file( fileName );

    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
    {
        DEBUG_LOG( QString( "No es pot escriure al fitxer " ) + fileName );
        return false;
    }

    QDataStream out( &file );
    int n = list.size();

    for ( int i = 0; i < n; i++ ) out << list.at( i );

    file.close();

    return true;
}


bool QExperimental3DExtension::saveDataAsText( const QList< QPair<int, Vector3> > &list, const QString &fileName, const QString &format, int base1, int base2 )
{
    QFile file( fileName );

    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
    {
        DEBUG_LOG( QString( "No es pot escriure al fitxer " ) + fileName );
        return false;
    }

    QTextStream out( &file );
    int n = list.size();

    for ( int i = 0; i < n; i++ ) out << format.arg( i + base1 ).arg( list.at( i ).first + base2 ).arg( list.at( i ).second.toString() ) << "\n";

    file.close();

    return true;
}


void QExperimental3DExtension::createConnections()
{
    connect(m_viewer, SIGNAL(volumeChanged(Volume*)), SLOT(setNewVolume(Volume*)));
    
    // visualització
    connect(m_backgroundColorPushButton, SIGNAL(clicked()), SLOT(chooseBackgroundColor()));
    connect(m_baseFullLightingRadioButton, SIGNAL(toggled(bool)), m_baseAmbientLabel, SLOT(setEnabled(bool)));
    connect(m_baseFullLightingRadioButton, SIGNAL(toggled(bool)), m_baseAmbientDoubleSpinBox, SLOT(setEnabled(bool)));
    connect(m_baseFullLightingRadioButton, SIGNAL(toggled(bool)), m_baseDiffuseLabel, SLOT(setEnabled(bool)));
    connect(m_baseFullLightingRadioButton, SIGNAL(toggled(bool)), m_baseDiffuseDoubleSpinBox, SLOT(setEnabled(bool)));
    connect(m_baseFullLightingRadioButton, SIGNAL(toggled(bool)), m_baseSpecularLabel, SLOT(setEnabled(bool)));
    connect(m_baseFullLightingRadioButton, SIGNAL(toggled(bool)), m_baseSpecularDoubleSpinBox, SLOT(setEnabled(bool)));
    connect(m_baseFullLightingRadioButton, SIGNAL(toggled(bool)), m_baseSpecularPowerLabel, SLOT(setEnabled(bool)));
    connect(m_baseFullLightingRadioButton, SIGNAL(toggled(bool)), m_baseSpecularPowerDoubleSpinBox, SLOT(setEnabled(bool)));
    connect( m_baseCoolWarmRadioButton, SIGNAL( toggled(bool) ), m_baseCoolWarmBLabel, SLOT( setEnabled(bool) ) );
    connect( m_baseCoolWarmRadioButton, SIGNAL( toggled(bool) ), m_baseCoolWarmBDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_baseCoolWarmRadioButton, SIGNAL( toggled(bool) ), m_baseCoolWarmYLabel, SLOT( setEnabled(bool) ) );
    connect( m_baseCoolWarmRadioButton, SIGNAL( toggled(bool) ), m_baseCoolWarmYDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_baseCoolWarmRadioButton, SIGNAL( toggled(bool) ), m_baseCoolWarmAlphaLabel, SLOT( setEnabled(bool) ) );
    connect( m_baseCoolWarmRadioButton, SIGNAL( toggled(bool) ), m_baseCoolWarmAlphaDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_baseCoolWarmRadioButton, SIGNAL( toggled(bool) ), m_baseCoolWarmBetaLabel, SLOT( setEnabled(bool) ) );
    connect( m_baseCoolWarmRadioButton, SIGNAL( toggled(bool) ), m_baseCoolWarmBetaDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect(m_baseVomiRadioButton, SIGNAL(toggled(bool)), SLOT(enableBaseVomi(bool)));
    connect( m_baseImiRadioButton, SIGNAL( toggled(bool) ), m_baseImiFactorLabel, SLOT( setEnabled(bool) ) );
    connect( m_baseImiRadioButton, SIGNAL( toggled(bool) ), m_baseImiFactorDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_baseVoxelSalienciesRadioButton, SIGNAL( toggled(bool) ), m_baseVoxelSalienciesFactorLabel, SLOT( setEnabled(bool) ) );
    connect( m_baseVoxelSalienciesRadioButton, SIGNAL( toggled(bool) ), m_baseVoxelSalienciesFactorDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_baseFilteringAmbientOcclusionRadioButton, SIGNAL( toggled(bool) ), m_baseFilteringAmbientOcclusionTypeComboBox, SLOT( setEnabled(bool) ) );
    connect( m_baseFilteringAmbientOcclusionRadioButton, SIGNAL( toggled(bool) ), m_baseFilteringAmbientOcclusionFactorLabel, SLOT( setEnabled(bool) ) );
    connect( m_baseFilteringAmbientOcclusionRadioButton, SIGNAL( toggled(bool) ), m_baseFilteringAmbientOcclusionFactorDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_baseFilteringAmbientOcclusionStipplingRadioButton, SIGNAL( toggled(bool) ), m_baseFilteringAmbientOcclusionStipplingThresholdLabel, SLOT( setEnabled(bool) ) );
    connect( m_baseFilteringAmbientOcclusionStipplingRadioButton, SIGNAL( toggled(bool) ), m_baseFilteringAmbientOcclusionStipplingThresholdDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_baseFilteringAmbientOcclusionStipplingRadioButton, SIGNAL( toggled(bool) ), m_baseFilteringAmbientOcclusionStipplingFactorLabel, SLOT( setEnabled(bool) ) );
    connect( m_baseFilteringAmbientOcclusionStipplingRadioButton, SIGNAL( toggled(bool) ), m_baseFilteringAmbientOcclusionStipplingFactorDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_additiveObscuranceVomiCheckBox, SIGNAL( toggled(bool) ), m_additiveObscuranceVomiWeightLabel, SLOT( setEnabled(bool) ) );
    connect( m_additiveObscuranceVomiCheckBox, SIGNAL( toggled(bool) ), m_additiveObscuranceVomiWeightDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_contourCheckBox, SIGNAL( toggled(bool) ), m_contourDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_renderingOkPushButton, SIGNAL( clicked() ), SLOT( render() ) );
    connect( m_obscuranceCheckBox, SIGNAL( toggled(bool) ), m_obscuranceFactorLabel, SLOT( setEnabled(bool) ) );
    connect( m_obscuranceCheckBox, SIGNAL( toggled(bool) ), m_obscuranceFactorDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_obscuranceCheckBox, SIGNAL( toggled(bool) ), m_obscuranceFiltersLabel, SLOT( setEnabled(bool) ) );
    connect( m_obscuranceCheckBox, SIGNAL( toggled(bool) ), m_obscuranceLowFilterLabel, SLOT( setEnabled(bool) ) );
    connect( m_obscuranceCheckBox, SIGNAL( toggled(bool) ), m_obscuranceLowFilterDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_obscuranceCheckBox, SIGNAL( toggled(bool) ), m_obscuranceHighFilterLabel, SLOT( setEnabled(bool) ) );
    connect( m_obscuranceCheckBox, SIGNAL( toggled(bool) ), m_obscuranceHighFilterDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect(m_vomiCheckBox, SIGNAL(toggled(bool)), SLOT(enableVomi(bool)));
    connect( m_vomiCoolWarmCheckBox, SIGNAL( toggled(bool) ), m_vomiCoolWarmYLabel, SLOT( setEnabled(bool) ) );
    connect( m_vomiCoolWarmCheckBox, SIGNAL( toggled(bool) ), m_vomiCoolWarmYDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_vomiCoolWarmCheckBox, SIGNAL( toggled(bool) ), m_vomiCoolWarmBLabel, SLOT( setEnabled(bool) ) );
    connect( m_vomiCoolWarmCheckBox, SIGNAL( toggled(bool) ), m_vomiCoolWarmBDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_vomiCoolWarmCheckBox, SIGNAL( toggled(bool) ), m_vomiCoolWarmFactorLabel, SLOT( setEnabled(bool) ) );
    connect( m_vomiCoolWarmCheckBox, SIGNAL( toggled(bool) ), m_vomiCoolWarmFactorDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_colorVomiCheckBox, SIGNAL( toggled(bool) ), m_colorVomiFactorLabel, SLOT( setEnabled(bool) ) );
    connect( m_colorVomiCheckBox, SIGNAL( toggled(bool) ), m_colorVomiFactorDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_opacityVomiCheckBox, SIGNAL( toggled(bool) ), SLOT( opacityVomiChecked(bool) ) );
    connect( m_opacitySaliencyCheckBox, SIGNAL( toggled(bool) ), SLOT( opacitySaliencyChecked(bool) ) );
    connect( m_opacityFilteringCheckBox, SIGNAL( toggled(bool) ), SLOT( opacityFilteringChecked(bool) ) );
    connect( m_opacityProbabilisticAmbientOcclusionCheckBox, SIGNAL( toggled(bool) ), SLOT( opacityProbabilisticAmbientOcclusionChecked(bool) ) );
    connect( m_filteringAmbientOcclusionCheckBox, SIGNAL( toggled(bool) ), m_filteringAmbientOcclusionTypeComboBox, SLOT( setEnabled(bool) ) );
    connect( m_filteringAmbientOcclusionCheckBox, SIGNAL( toggled(bool) ), m_filteringAmbientOcclusionLambdaLabel, SLOT( setEnabled(bool) ) );
    connect( m_filteringAmbientOcclusionCheckBox, SIGNAL( toggled(bool) ), m_filteringAmbientOcclusionLambdaDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_probabilisticAmbientOcclusionCheckBox, SIGNAL( toggled(bool) ), m_probabilisticAmbientOcclusionGammaLabel, SLOT( setEnabled(bool) ) );
    connect( m_probabilisticAmbientOcclusionCheckBox, SIGNAL( toggled(bool) ), m_probabilisticAmbientOcclusionGammaDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_probabilisticAmbientOcclusionCheckBox, SIGNAL( toggled(bool) ), m_probabilisticAmbientOcclusionFactorLabel, SLOT( setEnabled(bool) ) );
    connect( m_probabilisticAmbientOcclusionCheckBox, SIGNAL( toggled(bool) ), m_probabilisticAmbientOcclusionFactorDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_opacityVarianceCheckBox, SIGNAL( toggled(bool) ), m_opacityVarianceMaxLabel, SLOT( setEnabled(bool) ) );
    connect( m_opacityVarianceCheckBox, SIGNAL( toggled(bool) ), m_opacityVarianceMaxDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_celShadingCheckBox, SIGNAL( toggled(bool) ), m_celShadingQuantumsLabel, SLOT( setEnabled(bool) ) );
    connect( m_celShadingCheckBox, SIGNAL( toggled(bool) ), m_celShadingQuantumsSpinBox, SLOT( setEnabled(bool) ) );

    // funcions de transferència
    connect( m_loadTransferFunctionPushButton, SIGNAL( clicked() ), SLOT( loadTransferFunction() ) );
    connect( m_saveTransferFunctionPushButton, SIGNAL( clicked() ), SLOT( saveTransferFunction() ) );
    connect( m_addRecentTransferFunctionPushButton, SIGNAL( clicked() ), SLOT( addRecentTransferFunction() ) );
    connect( m_recentTransferFunctionsListView, SIGNAL( doubleClicked(const QModelIndex&) ), SLOT( setRecentTransferFunction(const QModelIndex&) ) );
    connect(m_innernessProportionalOpacityPushButton, SIGNAL(clicked()), SLOT(generateInnernessProportionalOpacityTransferFunction()));
    connect( m_transferFunctionOkPushButton, SIGNAL( clicked() ), SLOT( setTransferFunction() ) );

    // càmera
    connect( m_cameraGetPushButton, SIGNAL( clicked() ), SLOT( getCamera() ) );
    connect( m_cameraSetPushButton, SIGNAL( clicked() ), SLOT( setCamera() ) );
    connect( m_cameraLoadPushButton, SIGNAL( clicked() ), SLOT( loadCamera() ) );
    connect( m_cameraSavePushButton, SIGNAL( clicked() ), SLOT( saveCamera() ) );
    connect( m_cameraViewpointDistributionWidget, SIGNAL( numberOfViewpointsChanged(int) ), SLOT( setNumberOfViewpoints(int) ) );
    connect( m_viewpointPushButton, SIGNAL( clicked() ), SLOT( setViewpoint() ) );
    connect( m_tourPushButton, SIGNAL( clicked() ), SLOT( tour() ) );
    connect( m_saveNextTourCheckBox, SIGNAL( toggled(bool) ), m_saveNextTourLineEdit, SLOT( setEnabled(bool) ) );
    connect( m_saveNextTourCheckBox, SIGNAL( toggled(bool) ), m_saveNextTourPushButton, SLOT( setEnabled(bool) ) );
    connect( m_saveNextTourPushButton, SIGNAL( clicked() ), SLOT( getFileNameToSaveTour() ) );

    // obscurances
    connect( m_obscurancePushButton, SIGNAL( clicked() ), SLOT( computeCancelObscurance() ) );
    connect( m_obscuranceLoadPushButton, SIGNAL( clicked() ), SLOT( loadObscurance() ) );
    connect( m_obscuranceSavePushButton, SIGNAL( clicked() ), SLOT( saveObscurance() ) );

    // SMI
    connect( m_smiViewpointDistributionWidget, SIGNAL( numberOfViewpointsChanged(int) ), SLOT( setNumberOfSmiViewpoints(int) ) );
    connect( m_smiDefaultAxisCheckBox, SIGNAL( toggled(bool) ), m_smiViewpointDistributionWidget, SLOT( setDisabled(bool) ) );
    connect( m_smiDefaultAxisCheckBox, SIGNAL( toggled(bool) ), m_smiViewpointLabel, SLOT( setDisabled(bool) ) );
    connect( m_smiDefaultAxisCheckBox, SIGNAL( toggled(bool) ), m_smiViewpointSpinBox, SLOT( setDisabled(bool) ) );
    connect( m_smiPushButton, SIGNAL( clicked() ), SLOT( computeSmi() ) );
    connect( m_sliceUnstabilitiesPushButton, SIGNAL( clicked() ), SLOT( computeSliceUnstabilities() ) );
    connect( m_pmiPushButton, SIGNAL( clicked() ), SLOT( computePmi() ) );
    connect( m_propertySalienciesPushButton, SIGNAL( clicked() ), SLOT( computePropertySaliencies() ) );

    // VMI
    connect( m_vmiViewpointDistributionWidget, SIGNAL( numberOfViewpointsChanged(int) ), SLOT( setVmiOneViewpointMaximum(int) ) );
    connect( m_vmiOneViewpointCheckBox, SIGNAL( toggled(bool) ), m_vmiOneViewpointSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_saveViewedVolumePushButton, SIGNAL( clicked() ), SLOT( saveViewedVolume() ) );
    connect(m_loadHVPushButton, SIGNAL(clicked()), SLOT(loadHV()));
    connect(m_saveHVPushButton, SIGNAL(clicked()), SLOT(saveHV()));
    connect(m_loadHVzPushButton, SIGNAL(clicked()), SLOT(loadHVz()));
    connect(m_saveHVzPushButton, SIGNAL(clicked()), SLOT(saveHVz()));
    connect(m_loadHZPushButton, SIGNAL(clicked()), SLOT(loadHZ()));
    connect(m_saveHZPushButton, SIGNAL(clicked()), SLOT(saveHZ()));
    connect(m_loadHZvPushButton, SIGNAL(clicked()), SLOT(loadHZv()));
    connect(m_saveHZvPushButton, SIGNAL(clicked()), SLOT(saveHZv()));
    connect(m_loadHZVPushButton, SIGNAL(clicked()), SLOT(loadHZV()));
    connect(m_saveHZVPushButton, SIGNAL(clicked()), SLOT(saveHZV()));
    connect(m_loadVmiPushButton, SIGNAL(clicked()), SLOT(loadVmi()));
    connect(m_saveVmiPushButton, SIGNAL(clicked()), SLOT(saveVmi()));
    connect(m_loadVmi2PushButton, SIGNAL(clicked()), SLOT(loadVmi2()));
    connect(m_saveVmi2PushButton, SIGNAL(clicked()), SLOT(saveVmi2()));
    connect(m_loadVmi3PushButton, SIGNAL(clicked()), SLOT(loadVmi3()));
    connect(m_saveVmi3PushButton, SIGNAL(clicked()), SLOT(saveVmi3()));
    connect( m_loadMiPushButton, SIGNAL( clicked() ), SLOT( loadMi() ) );
    connect( m_saveMiPushButton, SIGNAL( clicked() ), SLOT( saveMi() ) );
    connect( m_loadViewpointUnstabilitiesPushButton, SIGNAL( clicked() ), SLOT( loadViewpointUnstabilities() ) );
    connect( m_saveViewpointUnstabilitiesPushButton, SIGNAL( clicked() ), SLOT( saveViewpointUnstabilities() ) );
    connect(m_loadVomiPushButton, SIGNAL(clicked()), SLOT(loadVomi()));
    connect(m_saveVomiPushButton, SIGNAL(clicked()), SLOT(saveVomi()));
    connect(m_loadVomi2PushButton, SIGNAL(clicked()), SLOT(loadVomi2()));
    connect(m_saveVomi2PushButton, SIGNAL(clicked()), SLOT(saveVomi2()));
    connect(m_loadVomi3PushButton, SIGNAL(clicked()), SLOT(loadVomi3()));
    connect(m_saveVomi3PushButton, SIGNAL(clicked()), SLOT(saveVomi3()));
    connect(m_loadViewpointVomiPushButton, SIGNAL(clicked()), SLOT(loadViewpointVomi()));
    connect(m_saveViewpointVomiPushButton, SIGNAL(clicked()), SLOT(saveViewpointVomi()));
    connect(m_loadViewpointVomi2PushButton, SIGNAL(clicked()), SLOT(loadViewpointVomi2()));
    connect(m_saveViewpointVomi2PushButton, SIGNAL(clicked()), SLOT(saveViewpointVomi2()));
    connect( m_loadColorVomiPalettePushButton, SIGNAL( clicked() ), SLOT( loadColorVomiPalette() ) );
    connect( m_loadColorVomiPushButton, SIGNAL( clicked() ), SLOT( loadColorVomi() ) );
    connect( m_saveColorVomiPushButton, SIGNAL( clicked() ), SLOT( saveColorVomi() ) );
    connect( m_computeEvmiOpacityUseOtherPushButton, SIGNAL( clicked() ), SLOT( loadEvmiOpacityOtherTransferFunction() ) );
    connect( m_loadEvmiOpacityPushButton, SIGNAL( clicked() ), SLOT( loadEvmiOpacity() ) );
    connect( m_saveEvmiOpacityPushButton, SIGNAL( clicked() ), SLOT( saveEvmiOpacity() ) );
    connect( m_loadEvmiVomiPushButton, SIGNAL( clicked() ), SLOT( loadEvmiVomi() ) );
    connect( m_saveEvmiVomiPushButton, SIGNAL( clicked() ), SLOT( saveEvmiVomi() ) );
    connect( m_computeBestViewsCheckBox, SIGNAL( toggled(bool) ), m_computeBestViewsNRadioButton, SLOT( setEnabled(bool) ) );
    connect( m_computeBestViewsCheckBox, SIGNAL( toggled(bool) ), m_computeBestViewsNSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_computeBestViewsCheckBox, SIGNAL( toggled(bool) ), m_computeBestViewsThresholdRadioButton, SLOT( setEnabled(bool) ) );
    connect( m_computeBestViewsCheckBox, SIGNAL( toggled(bool) ), m_computeBestViewsThresholdDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_loadBestViewsPushButton, SIGNAL( clicked() ), SLOT( loadBestViews() ) );
    connect( m_saveBestViewsPushButton, SIGNAL( clicked() ), SLOT( saveBestViews() ) );
    connect( m_loadGuidedTourPushButton, SIGNAL( clicked() ), SLOT( loadGuidedTour() ) );
    connect( m_saveGuidedTourPushButton, SIGNAL( clicked() ), SLOT( saveGuidedTour() ) );
    connect( m_computeExploratoryTourCheckBox, SIGNAL( toggled(bool) ), m_computeExploratoryTourThresholdLabel, SLOT( setEnabled(bool) ) );
    connect( m_computeExploratoryTourCheckBox, SIGNAL( toggled(bool) ), m_computeExploratoryTourThresholdDoubleSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_loadExploratoryTourPushButton, SIGNAL( clicked() ), SLOT( loadExploratoryTour() ) );
    connect( m_saveExploratoryTourPushButton, SIGNAL( clicked() ), SLOT( saveExploratoryTour() ) );
    connect( m_computeVmiPushButton, SIGNAL( clicked() ), SLOT( computeSelectedVmi() ) );
    connect( m_tourBestViewsPushButton, SIGNAL( clicked() ), SLOT( tourBestViews() ) );
    connect( m_guidedTourPushButton, SIGNAL( clicked() ), SLOT( guidedTour() ) );
    connect( m_exploratoryTourPushButton, SIGNAL( clicked() ), SLOT( exploratoryTour() ) );
    connect(m_vomiGradientPushButton, SIGNAL(clicked()), SLOT(computeVomiGradient()));
    connect(m_vomi2GradientPushButton, SIGNAL(clicked()), SLOT(computeVomi2Gradient()));

    // VMIi
    connect( m_vmiiViewpointDistributionWidget, SIGNAL( numberOfViewpointsChanged(int) ), SLOT( setVmiiOneViewpointMaximum(int) ) );
    connect( m_vmiiOneViewpointCheckBox, SIGNAL( toggled(bool) ), m_vmiiOneViewpointSpinBox, SLOT( setEnabled(bool) ) );
    connect( m_saveViewedVolumeIPushButton, SIGNAL( clicked() ), SLOT( saveViewedVolumeI() ) );
    connect(m_loadHIPushButton, SIGNAL(clicked()), SLOT(loadHI()));
    connect(m_saveHIPushButton, SIGNAL(clicked()), SLOT(saveHI()));
    connect(m_loadHIvPushButton, SIGNAL(clicked()), SLOT(loadHIv()));
    connect(m_saveHIvPushButton, SIGNAL(clicked()), SLOT(saveHIv()));
    connect(m_loadHIVPushButton, SIGNAL(clicked()), SLOT(loadHIV()));
    connect(m_saveHIVPushButton, SIGNAL(clicked()), SLOT(saveHIV()));
    connect( m_loadVmiiPushButton, SIGNAL( clicked() ), SLOT( loadVmii() ) );
    connect( m_saveVmiiPushButton, SIGNAL( clicked() ), SLOT( saveVmii() ) );
    connect( m_loadMiiPushButton, SIGNAL( clicked() ), SLOT( loadMii() ) );
    connect( m_saveMiiPushButton, SIGNAL( clicked() ), SLOT( saveMii() ) );
    connect( m_loadViewpointUnstabilitiesIPushButton, SIGNAL( clicked() ), SLOT( loadViewpointUnstabilitiesI() ) );
    connect( m_saveViewpointUnstabilitiesIPushButton, SIGNAL( clicked() ), SLOT( saveViewpointUnstabilitiesI() ) );
    connect( m_loadImiPushButton, SIGNAL( clicked() ), SLOT( loadImi() ) );
    connect( m_saveImiPushButton, SIGNAL( clicked() ), SLOT( saveImi() ) );
    connect( m_computeVmiiPushButton, SIGNAL( clicked() ), SLOT( computeSelectedVmii() ) );
    connect( m_colorTransferFunctionFromImiPushButton, SIGNAL( clicked() ), SLOT( generateColorTransferFunctionFromImi() ) );
    connect( m_opacityTransferFunctionFromImiPushButton, SIGNAL( clicked() ), SLOT( generateOpacityTransferFunctionFromImi() ) );
    connect( m_transferFunctionFromImiPushButton, SIGNAL( clicked() ), SLOT( generateTransferFunctionFromImi() ) );
    connect(m_transferFunctionFromIntensityClusteringPushButton, SIGNAL(clicked()), SLOT(generateTransferFunctionFromIntensityClusters()));
    connect(m_geneticTransferFunctionFromIntensityClusteringPushButton, SIGNAL(clicked()), SLOT(generateAndEvolveTransferFunctionFromIntensityClusters()));
    connect(m_fineTuneGeneticTransferFunctionFromIntensityClusteringPushButton, SIGNAL(clicked()), SLOT(fineTuneGeneticTransferFunctionFromIntensityClusters()));
    connect(m_geneticTransferFunctionFromIntensityClusteringWeightsUniformRadioButton, SIGNAL(toggled(bool)), SLOT(fillWeigthsEditor()));
    connect(m_geneticTransferFunctionFromIntensityClusteringWeightsVolumeDistributionRadioButton, SIGNAL(toggled(bool)), SLOT(fillWeigthsEditor()));
    connect(m_geneticTransferFunctionFromIntensityClusteringWeightsManualRadioButton, SIGNAL(toggled(bool)), SLOT(fillWeigthsEditor()));

    // Program
    connect( m_loadAndRunProgramPushButton, SIGNAL( clicked() ), SLOT( loadAndRunProgram() ) );

    // Filtering
    connect( m_filteringGaussianPushButton, SIGNAL( clicked() ), SLOT( gaussianFilter() ) );
    connect( m_filteringBoxMeanPushButton, SIGNAL( clicked() ), SLOT( boxMeanFilter() ) );
    connect( m_probabilisticAmbientOcclusionGaussianChebychevPushButton, SIGNAL( clicked() ), SLOT( probabilisticAmbientOcclusionGaussianChebychev() ) );
    connect( m_probabilisticAmbientOcclusionBoxMeanChebychevPushButton, SIGNAL( clicked() ), SLOT( probabilisticAmbientOcclusionBoxMeanChebychev() ) );
    connect( m_probabilisticAmbientOcclusionGaussianPushButton, SIGNAL( clicked() ), SLOT( probabilisticAmbientOcclusionGaussian() ) );
    connect( m_probabilisticAmbientOcclusionCubePushButton, SIGNAL( clicked() ), SLOT( probabilisticAmbientOcclusionCube() ) );
    connect( m_probabilisticAmbientOcclusionSpherePushButton, SIGNAL( clicked() ), SLOT( probabilisticAmbientOcclusionSphere() ) );
    connect( m_probabilisticAmbientOcclusionTangentSphereVariancePushButton, SIGNAL( clicked() ), SLOT( probabilisticAmbientOcclusionTangentSphereVariance() ) );
    connect( m_probabilisticAmbientOcclusionTangentSphereCdfPushButton, SIGNAL( clicked() ), SLOT( probabilisticAmbientOcclusionTangentSphereCdf() ) );
    connect( m_probabilisticAmbientOcclusionTangentSphereGaussianPushButton, SIGNAL( clicked() ), SLOT( probabilisticAmbientOcclusionTangentSphereGaussian() ) );
    connect( m_probabilisticAmbientOcclusionGradientPushButton, SIGNAL( clicked() ), SLOT( probabilisticAmbientOcclusionGradient() ) );
    connect( m_volumeVariancePushButton, SIGNAL( clicked() ), SLOT( volumeVariance() ) );
}


QString QExperimental3DExtension::getFileNameToLoad( const QString &settingsDirKey, const QString &caption, const QString &filter )
{
    Settings settings;

    QString dir = settings.getValue( settingsDirKey ).toString();
    QString fileName = QFileDialog::getOpenFileName( this, caption, dir, filter );

    if ( !fileName.isNull() )
    {
        QFileInfo fileInfo( fileName );
        settings.setValue( settingsDirKey, fileInfo.absolutePath() );
    }

    return fileName;
}


QString QExperimental3DExtension::getFileNameToSave( const QString &settingsDirKey, const QString &caption, const QString &filter, const QString &defaultSuffix )
{
    QString fileName;
    Settings settings;

    QString dir = settings.getValue( settingsDirKey ).toString();
    QFileDialog saveDialog( this, caption, dir, filter );
    saveDialog.setAcceptMode( QFileDialog::AcceptSave );
    saveDialog.setDefaultSuffix( defaultSuffix );

    if ( saveDialog.exec() == QDialog::Accepted )
    {
        fileName = saveDialog.selectedFiles().first();
        QFileInfo fileInfo( fileName );
        settings.setValue( settingsDirKey, fileInfo.absolutePath() );
    }

    return fileName;
}


void QExperimental3DExtension::loadTransferFunction()
{
    QString transferFunctionFileName = getFileNameToLoad( Experimental3DSettings::TransferFunctionDir, tr("Load transfer function"), tr("XML files (*.xml);;Transfer function files (*.tf);;All files (*)") );
    if ( !transferFunctionFileName.isNull() ) loadTransferFunction( transferFunctionFileName );
}


void QExperimental3DExtension::loadTransferFunction( const QString &fileName )
{
    TransferFunction *transferFunction;

    if ( fileName.endsWith( ".xml" ) ) transferFunction = TransferFunctionIO::fromXmlFile( fileName );
    else transferFunction = TransferFunctionIO::fromFile( fileName );

    m_transferFunctionEditor->setTransferFunction( *transferFunction );

    m_recentTransferFunctions << *transferFunction;
    int row = m_recentTransferFunctionsModel->rowCount();
    m_recentTransferFunctionsModel->insertRow( row );
    QString name = transferFunction->name();
    if ( name.isEmpty() ) name = tr("<unnamed>");
    m_recentTransferFunctionsModel->setData( m_recentTransferFunctionsModel->index( row, 0 ), name );

    delete transferFunction;

    setTransferFunction();
}


void QExperimental3DExtension::saveTransferFunction()
{
    QString transferFunctionFileName = getFileNameToSave( Experimental3DSettings::TransferFunctionDir, tr("Save transfer function"), tr("XML files (*.xml);;Transfer function files (*.tf);;All files (*)"), "xml" );

    if ( !transferFunctionFileName.isNull() ) saveTransferFunction( transferFunctionFileName );
}


void QExperimental3DExtension::saveTransferFunction( const QString &fileName )
{
    if ( fileName.endsWith( ".xml" ) ) TransferFunctionIO::toXmlFile( fileName, m_transferFunctionEditor->transferFunction() );
    else TransferFunctionIO::toFile( fileName, m_transferFunctionEditor->transferFunction() );

    m_recentTransferFunctions << m_transferFunctionEditor->transferFunction();
    int row = m_recentTransferFunctionsModel->rowCount();
    m_recentTransferFunctionsModel->insertRow( row );
    QString name = m_transferFunctionEditor->transferFunction().name();
    if ( name.isEmpty() ) name = tr("<unnamed>");
    m_recentTransferFunctionsModel->setData( m_recentTransferFunctionsModel->index( row, 0 ), name );
}


void QExperimental3DExtension::addRecentTransferFunction()
{
    m_recentTransferFunctions << m_transferFunctionEditor->transferFunction();
    int row = m_recentTransferFunctionsModel->rowCount();
    m_recentTransferFunctionsModel->insertRow( row );
    QString name = m_transferFunctionEditor->transferFunction().name();
    if ( name.isEmpty() ) name = tr("<unnamed>");
    m_recentTransferFunctionsModel->setData( m_recentTransferFunctionsModel->index( row, 0 ), name );
}


void QExperimental3DExtension::setRecentTransferFunction( const QModelIndex &index )
{
    m_transferFunctionEditor->setTransferFunction( m_recentTransferFunctions.at( index.row() ) );
    setTransferFunction();
}


void QExperimental3DExtension::setTransferFunction( bool render )
{
    m_volume->setTransferFunction( m_transferFunctionEditor->transferFunction() );
    if ( render ) m_viewer->render();
}


void QExperimental3DExtension::tour( const QList<Vector3> &viewpoints, double speedFactor )
{
    if ( viewpoints.isEmpty() ) return;

    uint frameCounter = 0;
    QString tourFileName = m_saveNextTourLineEdit->text();
    bool saveTour = m_saveNextTourCheckBox->isChecked();

    const double ALMOST_1 = 0.9;

    int *dimensions = m_volume->getImage()->GetDimensions();
    int maxDimension = qMax( qMax( dimensions[0], dimensions[1] ), dimensions[2] );
    double maxDistance = speedFactor * maxDimension / 4.0;

    DEBUG_LOG( "Tour:" );

    Vector3 previousPoint = viewpoints.at( 0 );
    DEBUG_LOG( previousPoint.toString() );

    Vector3 currentPoint = previousPoint;
    setViewpoint( currentPoint );
    if ( saveTour ) m_viewer->screenshot( tourFileName.arg( frameCounter++, 8, 10, QChar( '0' ) ) );

    for ( int i = 1; i < viewpoints.size(); i++ )
    {
        Vector3 nextPoint = viewpoints.at( i );
        double nextRadius = nextPoint.length();

        // Mirem si s'ha d'afegir un punt intermig
        {
            Vector3 c = currentPoint;
            c.normalize();
            Vector3 n = nextPoint;
            n.normalize();

            if ( c * n < -ALMOST_1 ) // la línia entre els punts passa pel centre del volum
            {
                DEBUG_LOG( QString( "punt intermig: c = %1, n = %2, c * n = %3" ).arg( c.toString() ).arg( n.toString() ).arg( c * n ) );
                // afegim un punt intermig (0,0,radi) o (radi,0,0)
                double radius = ( currentPoint.length() + nextRadius ) / 2.0;

                if ( qAbs( c * Vector3( 0.0, 0.0, 1.0 ) ) > ALMOST_1 ) nextPoint.set( radius, 0.0, 0.0 );
                else nextPoint.set( 0.0, 0.0, radius );

                i--;
            }
        }

        while ( currentPoint != nextPoint )
        {
            Vector3 direction = nextPoint - currentPoint;

            if ( direction.length() < maxDistance ) currentPoint = nextPoint;
            else
            {
                double currentRadius = currentPoint.length();
                Vector3 nextCurrentPoint;

                do
                {
                    direction.normalize() *= maxDistance;  // posem la direcció a la llargada desitjada
                    nextCurrentPoint = currentPoint + direction;

                    double currentToNextCurrent = ( nextCurrentPoint - currentPoint ).length();
                    double nextCurrentToNext = ( nextPoint - nextCurrentPoint ).length();
                    double a = currentToNextCurrent / ( currentToNextCurrent + nextCurrentToNext ); // valor per interpolar el radi
                    double nextCurrentRadius = a * currentRadius + ( 1.0 - a ) * nextRadius;

                    nextCurrentPoint.normalize() *= nextCurrentRadius;  // posem el nou punt a la distància correcta
                    direction = nextCurrentPoint - currentPoint;
                } while ( direction.length() <= maxDistance - 1.0 || direction.length() >= maxDistance + 1.0 );

                currentPoint = nextCurrentPoint;
            }

            setViewpoint( currentPoint );
            if ( saveTour ) m_viewer->screenshot( tourFileName.arg( frameCounter++, 8, 10, QChar( '0' ) ) );
        }

        DEBUG_LOG( nextPoint.toString() );
    }
}


void QExperimental3DExtension::chooseBackgroundColor()
{
    QColor color = QColorDialog::getColor( m_viewer->getBackgroundColor(), this );
    if ( color.isValid() ) m_viewer->setBackgroundColor( color );
}


float passIfNegative( float f )
{
    return f < 0.0f ? f : 0.0f;
}


void QExperimental3DExtension::render()
{
    m_volume->setInterpolation(static_cast<Experimental3DVolume::Interpolation>(m_interpolationComboBox->currentIndex()));
    m_volume->setGradientEstimator(static_cast<Experimental3DVolume::GradientEstimator>(m_gradientEstimatorComboBox->currentIndex()));

    m_volume->resetShadingOptions();

    if (m_baseAmbientLightingRadioButton->isChecked()) m_volume->addAmbientLighting();
    else if (m_baseFullLightingRadioButton->isChecked())
    {
        m_viewer->updateShadingTable();
        m_volume->addFullLighting(m_baseAmbientDoubleSpinBox->value(), m_baseDiffuseDoubleSpinBox->value(), m_baseSpecularDoubleSpinBox->value(), m_baseSpecularPowerDoubleSpinBox->value());
    }
    else if ( m_baseCoolWarmRadioButton->isChecked() ) m_volume->addCoolWarm( m_baseCoolWarmBDoubleSpinBox->value(), m_baseCoolWarmYDoubleSpinBox->value(), m_baseCoolWarmAlphaDoubleSpinBox->value(),
                                                                              m_baseCoolWarmBetaDoubleSpinBox->value() );
    else if ( m_baseWhiteRadioButton->isChecked() ) m_volume->addWhite();
    else if (m_baseVomiRadioButton->isChecked())
    {
        if (m_baseVomi1RadioButton->isChecked()) m_volume->addVomi(m_vomi, m_minimumVomi, m_maximumVomi, m_baseVomiFactorDoubleSpinBox->value());
        else if (m_baseVomi2RadioButton->isChecked()) m_volume->addVomi(m_vomi2, m_minimumVomi2, m_maximumVomi2, m_baseVomiFactorDoubleSpinBox->value());
        else if (m_baseVomi3RadioButton->isChecked()) m_volume->addVomi(m_vomi3, m_minimumVomi3, m_maximumVomi3, m_baseVomiFactorDoubleSpinBox->value());
    }
    //else if (m_baseVomiRadioButton->isChecked()) m_volume->addVoxelSaliencies(m_vomi, m_maximumVomi, m_baseVomiFactorDoubleSpinBox->value());
    else if ( m_baseImiRadioButton->isChecked() ) m_volume->addImi( m_imi, m_maximumImi, m_baseImiFactorDoubleSpinBox->value() );
    else if ( m_baseVoxelSalienciesRadioButton->isChecked() ) m_volume->addVoxelSaliencies( m_voxelSaliencies, m_maximumSaliency, m_baseVoxelSalienciesFactorDoubleSpinBox->value() );
    //else if ( m_baseVoxelSalienciesRadioButton->isChecked() ) m_volume->addVomi( m_voxelSaliencies, m_maximumSaliency, m_baseVoxelSalienciesFactorDoubleSpinBox->value() );
    else if ( m_baseFilteringAmbientOcclusionRadioButton->isChecked() )
    {
        switch ( m_baseFilteringAmbientOcclusionTypeComboBox->currentIndex() )
        {
            case 0: // direct
                m_volume->addFilteringAmbientOcclusionMap( m_spatialImportanceFunction, m_maximumSpatialImportanceFunction, m_baseFilteringAmbientOcclusionFactorDoubleSpinBox->value() );
                break;
            case 1: // absolute
                {
                    QVector<float> absFiltering = QtConcurrent::blockingMapped( m_spatialImportanceFunction, qAbs<float> );
                    m_volume->addFilteringAmbientOcclusionMap( absFiltering, m_maximumSpatialImportanceFunction, m_baseFilteringAmbientOcclusionFactorDoubleSpinBox->value() );
                }
                break;
            case 2: // negatives
                {
                    QVector<float> negativeFiltering = QtConcurrent::blockingMapped( m_spatialImportanceFunction, passIfNegative );
                    m_volume->addFilteringAmbientOcclusionMap( negativeFiltering, m_maximumSpatialImportanceFunction, m_baseFilteringAmbientOcclusionFactorDoubleSpinBox->value() );
                }
                break;
        }
    }
    else if ( m_baseFilteringAmbientOcclusionStipplingRadioButton->isChecked() )
    {
        QVector<float> absFiltering = QtConcurrent::blockingMapped( m_spatialImportanceFunction, qAbs<float> );
        m_volume->addFilteringAmbientOcclusionStippling( absFiltering, m_maximumSpatialImportanceFunction, m_baseFilteringAmbientOcclusionStipplingThresholdDoubleSpinBox->value(),
                                                         m_baseFilteringAmbientOcclusionStipplingFactorDoubleSpinBox->value() );
    }

    if ( m_contourCheckBox->isChecked() ) m_volume->addContour( m_contourDoubleSpinBox->value() );
    if ( m_obscuranceCheckBox->isChecked() ) m_volume->addObscurance( m_obscurance, m_obscuranceFactorDoubleSpinBox->value(), m_obscuranceLowFilterDoubleSpinBox->value(), m_obscuranceHighFilterDoubleSpinBox->value(),
                                                                      m_additiveObscuranceVomiCheckBox->isChecked(), m_additiveObscuranceVomiWeightDoubleSpinBox->value() );
    if (m_vomiCheckBox->isChecked())
    {
        if (m_vomi1RadioButton->isChecked())
            m_volume->addVomi(m_vomi, m_minimumVomi, m_maximumVomi, m_vomiFactorDoubleSpinBox->value(), m_additiveObscuranceVomiCheckBox->isChecked(), m_additiveObscuranceVomiWeightDoubleSpinBox->value());
        else if (m_vomi2RadioButton->isChecked())
            m_volume->addVomi(m_vomi2, m_minimumVomi2, m_maximumVomi2, m_vomiFactorDoubleSpinBox->value(), m_additiveObscuranceVomiCheckBox->isChecked(), m_additiveObscuranceVomiWeightDoubleSpinBox->value());
        else if (m_vomi3RadioButton->isChecked())
            m_volume->addVomi(m_vomi3, m_minimumVomi3, m_maximumVomi3, m_vomiFactorDoubleSpinBox->value(), m_additiveObscuranceVomiCheckBox->isChecked(), m_additiveObscuranceVomiWeightDoubleSpinBox->value());
    }
    if ( m_vomiCoolWarmCheckBox->isChecked() ) m_volume->addVomiCoolWarm( m_vomi, m_maximumVomi, m_vomiCoolWarmFactorDoubleSpinBox->value(),
                                                                          m_vomiCoolWarmYDoubleSpinBox->value(), m_vomiCoolWarmBDoubleSpinBox->value() );
    if ( m_colorVomiCheckBox->isChecked() ) m_volume->addColorVomi( m_colorVomi, m_maximumColorVomi, m_colorVomiFactorDoubleSpinBox->value() );
    if ( m_opacityVomiCheckBox->isChecked() ) m_volume->addOpacity( m_vomi, m_maximumVomi, m_opacityLowThresholdDoubleSpinBox->value(), m_opacityLowFactorDoubleSpinBox->value(),
                                                                                           m_opacityHighThresholdDoubleSpinBox->value(), m_opacityHighFactorDoubleSpinBox->value() );
    if ( m_opacitySaliencyCheckBox->isChecked() ) m_volume->addOpacity( m_voxelSaliencies, m_maximumSaliency, m_opacityLowThresholdDoubleSpinBox->value(), m_opacityLowFactorDoubleSpinBox->value(),
                                                                                                              m_opacityHighThresholdDoubleSpinBox->value(), m_opacityHighFactorDoubleSpinBox->value() );
    if ( m_opacityFilteringCheckBox->isChecked() )
    {
        QVector<float> absFiltering = QtConcurrent::blockingMapped( m_spatialImportanceFunction, qAbs<float> );
        m_volume->addOpacity( absFiltering, m_maximumSpatialImportanceFunction, m_opacityLowThresholdDoubleSpinBox->value(), m_opacityLowFactorDoubleSpinBox->value(),
                                                                                m_opacityHighThresholdDoubleSpinBox->value(), m_opacityHighFactorDoubleSpinBox->value() );
    }
    if ( m_opacityProbabilisticAmbientOcclusionCheckBox->isChecked() )
        m_volume->addOpacity( m_probabilisticAmbientOcclusion, 1.0f, m_opacityLowThresholdDoubleSpinBox->value(), m_opacityLowFactorDoubleSpinBox->value(),
                                                                     m_opacityHighThresholdDoubleSpinBox->value(), m_opacityHighFactorDoubleSpinBox->value() );
    if ( m_filteringAmbientOcclusionCheckBox->isChecked() )
    {
        switch ( m_filteringAmbientOcclusionTypeComboBox->currentIndex() )
        {
            case 0: // direct
                m_volume->addFilteringAmbientOcclusion( m_spatialImportanceFunction, m_maximumSpatialImportanceFunction, m_filteringAmbientOcclusionLambdaDoubleSpinBox->value() );
                break;
            case 1: // absolute
                {
                    QVector<float> absFiltering = QtConcurrent::blockingMapped( m_spatialImportanceFunction, qAbs<float> );
                    m_volume->addFilteringAmbientOcclusion( absFiltering, m_maximumSpatialImportanceFunction, m_filteringAmbientOcclusionLambdaDoubleSpinBox->value() );
                }
                break;
            case 2: // negatives
                {
                    QVector<float> negativeFiltering = QtConcurrent::blockingMapped( m_spatialImportanceFunction, passIfNegative );
                    m_volume->addFilteringAmbientOcclusion( negativeFiltering, m_maximumSpatialImportanceFunction, m_filteringAmbientOcclusionLambdaDoubleSpinBox->value() );
                }
                break;
        }
    }
    if ( m_probabilisticAmbientOcclusionCheckBox->isChecked() )
        m_volume->addVomiGamma( m_probabilisticAmbientOcclusion, 1.0f, m_probabilisticAmbientOcclusionFactorDoubleSpinBox->value(), m_probabilisticAmbientOcclusionGammaDoubleSpinBox->value(),
                                m_additiveObscuranceVomiCheckBox->isChecked(), m_additiveObscuranceVomiWeightDoubleSpinBox->value() );
    if ( m_opacityVarianceCheckBox->isChecked() ) m_volume->addOpacity( m_volumeVariance, m_opacityVarianceMaxDoubleSpinBox->value() );
    if ( m_celShadingCheckBox->isChecked() ) m_volume->addCelShading( m_celShadingQuantumsSpinBox->value() );

    m_viewer->render();
}


void QExperimental3DExtension::getCamera()
{
    Vector3 position, focus, up;

    m_viewer->getCamera( position, focus, up );

    m_cameraPositionXDoubleSpinBox->setValue( position.x );
    m_cameraPositionYDoubleSpinBox->setValue( position.y );
    m_cameraPositionZDoubleSpinBox->setValue( position.z );

    m_cameraFocusXDoubleSpinBox->setValue( focus.x );
    m_cameraFocusYDoubleSpinBox->setValue( focus.y );
    m_cameraFocusZDoubleSpinBox->setValue( focus.z );

    m_cameraUpXDoubleSpinBox->setValue( up.x );
    m_cameraUpYDoubleSpinBox->setValue( up.y );
    m_cameraUpZDoubleSpinBox->setValue( up.z );
}


void QExperimental3DExtension::setCamera()
{
    Vector3 position, focus, up;

    position.x = m_cameraPositionXDoubleSpinBox->value();
    position.y = m_cameraPositionYDoubleSpinBox->value();
    position.z = m_cameraPositionZDoubleSpinBox->value();

    focus.x = m_cameraFocusXDoubleSpinBox->value();
    focus.y = m_cameraFocusYDoubleSpinBox->value();
    focus.z = m_cameraFocusZDoubleSpinBox->value();

    up.x = m_cameraUpXDoubleSpinBox->value();
    up.y = m_cameraUpYDoubleSpinBox->value();
    up.z = m_cameraUpZDoubleSpinBox->value();

    m_viewer->setCamera( position, focus, up );
}


void QExperimental3DExtension::loadCamera()
{
    QString cameraFileName = getFileNameToLoad( Experimental3DSettings::CameraDir, tr("Load camera parameters"), tr("Camera files (*.cam);;All files (*)") );
    if ( !cameraFileName.isNull() ) loadCamera( cameraFileName );
}


void QExperimental3DExtension::loadCamera( const QString &fileName )
{
    QFile cameraFile( fileName );

    if ( !cameraFile.open( QFile::ReadOnly | QFile::Text ) )
    {
        DEBUG_LOG( QString( "No es pot llegir el fitxer " ) + fileName );
        ERROR_LOG( QString( "No es pot llegir el fitxer " ) + fileName );
        if ( m_interactive ) QMessageBox::warning( this, tr("Can't load"), QString( tr("Can't load from file ") ) + fileName );
        return;
    }

    QTextStream in( &cameraFile );

    Vector3 position, focus, up;

    if ( !in.atEnd() ) in >> position.x;
    if ( !in.atEnd() ) in >> position.y;
    if ( !in.atEnd() ) in >> position.z;

    if ( !in.atEnd() ) in >> focus.x;
    if ( !in.atEnd() ) in >> focus.y;
    if ( !in.atEnd() ) in >> focus.z;

    if ( !in.atEnd() ) in >> up.x;
    if ( !in.atEnd() ) in >> up.y;
    if ( !in.atEnd() ) in >> up.z;

    m_cameraPositionXDoubleSpinBox->setValue( position.x );
    m_cameraPositionYDoubleSpinBox->setValue( position.y );
    m_cameraPositionZDoubleSpinBox->setValue( position.z );

    m_cameraFocusXDoubleSpinBox->setValue( focus.x );
    m_cameraFocusYDoubleSpinBox->setValue( focus.y );
    m_cameraFocusZDoubleSpinBox->setValue( focus.z );

    m_cameraUpXDoubleSpinBox->setValue( up.x );
    m_cameraUpYDoubleSpinBox->setValue( up.y );
    m_cameraUpZDoubleSpinBox->setValue( up.z );

    cameraFile.close();

    setCamera();
}


void QExperimental3DExtension::saveCamera()
{
    QString cameraFileName = getFileNameToSave( Experimental3DSettings::CameraDir, tr("Save camera parameters"), tr("Camera files (*.cam);;All files (*)"), "cam" );

    if ( !cameraFileName.isNull() )
    {
        QFile cameraFile( cameraFileName );

        if ( !cameraFile.open( QFile::WriteOnly | QFile::Truncate | QFile::Text ) )
        {
            ERROR_LOG( QString( "No es pot escriure al fitxer " ) + cameraFileName );
            if ( m_interactive ) QMessageBox::warning( this, tr("Can't save"), QString( tr("Can't save to file ") ) + cameraFileName );
            return;
        }

        QTextStream out( &cameraFile );

        out << m_cameraPositionXDoubleSpinBox->value() << "\n";
        out << m_cameraPositionYDoubleSpinBox->value() << "\n";
        out << m_cameraPositionZDoubleSpinBox->value() << "\n";

        out << m_cameraFocusXDoubleSpinBox->value() << "\n";
        out << m_cameraFocusYDoubleSpinBox->value() << "\n";
        out << m_cameraFocusZDoubleSpinBox->value() << "\n";

        out << m_cameraUpXDoubleSpinBox->value() << "\n";
        out << m_cameraUpYDoubleSpinBox->value() << "\n";
        out << m_cameraUpZDoubleSpinBox->value() << "\n";

        out.flush();
        cameraFile.close();
    }
}


void QExperimental3DExtension::setNumberOfViewpoints( int numberOfViewpoints )
{
    m_viewpointSpinBox->setMaximum( numberOfViewpoints );
}


void QExperimental3DExtension::setViewpoint()
{
    Vector3 position, focus, up;
    m_viewer->getCamera( position, focus, up );

    float distance = ( position - focus ).length();

    ViewpointGenerator viewpointGenerator;

    if ( m_cameraViewpointDistributionWidget->isUniform() )
    {
        switch ( m_cameraViewpointDistributionWidget->numberOfViewpoints() )
        {
            case 4: viewpointGenerator.setToUniform4( distance ); break;
            case 6: viewpointGenerator.setToUniform6( distance ); break;
            case 8: viewpointGenerator.setToUniform8( distance ); break;
            case 12: viewpointGenerator.setToUniform12( distance ); break;
            case 20: viewpointGenerator.setToUniform20( distance ); break;
            default: Q_ASSERT_X( false, "setViewpoint", qPrintable( QString( "Nombre de punts de vista uniformes incorrecte: %1" ).arg( m_cameraViewpointDistributionWidget->numberOfViewpoints() ) ) );
        }
    }
    else viewpointGenerator.setToQuasiUniform( m_cameraViewpointDistributionWidget->recursionLevel(), distance );

    setViewpoint( viewpointGenerator.viewpoint( m_viewpointSpinBox->value() - 1 ) );
}


void QExperimental3DExtension::setViewpoint( const Vector3 &viewpoint )
{
    m_viewer->setCamera( viewpoint, Vector3(), ViewpointGenerator::up( viewpoint ) );
}


void QExperimental3DExtension::tour()
{
    Vector3 position, focus, up;
    m_viewer->getCamera( position, focus, up );
    float distance = ( position - focus ).length();
    ViewpointGenerator viewpointGenerator = m_cameraViewpointDistributionWidget->viewpointGenerator( distance );
    QVector<Vector3> viewpoints = viewpointGenerator.viewpoints();

    QStringList indices = m_tourLineEdit->text().split( ',' );
    QList<Vector3> tourViewpoints;

    for ( int i = 0; i < indices.size(); i++ )
    {
        int index = indices.at( i ).toInt() - 1;
        if ( index >= 0 && index < viewpoints.size() ) tourViewpoints << viewpoints.at( index );
    }

    tour( tourViewpoints, m_tourSpeedDoubleSpinBox->value() );
}


void QExperimental3DExtension::computeCancelObscurance()
{
    if ( !m_computingObscurance )
    {
        m_computingObscurance = true;

        if ( m_obscuranceCheckBox->isChecked() )
        {
            m_obscuranceCheckBox->setChecked( false );
            this->render();
        }

        m_obscuranceCheckBox->setEnabled( false );

        delete m_obscuranceMainThread;          // esborrem el thread d'abans
        delete m_obscurance; m_obscurance = 0;  // esborrem l'obscurança d'abans

        int numberOfDirections;
        if ( m_obscuranceViewpointDistributionWidget->isUniform() )
            numberOfDirections = -1 * m_obscuranceViewpointDistributionWidget->numberOfViewpoints();
        else
            numberOfDirections = m_obscuranceViewpointDistributionWidget->recursionLevel();

        m_obscuranceMainThread = new ObscuranceMainThread( numberOfDirections,
                                                           m_obscuranceMaximumDistanceDoubleSpinBox->value(),
                                                           static_cast<ObscuranceMainThread::Function>( m_obscuranceFunctionComboBox->currentIndex() ),
                                                           static_cast<ObscuranceMainThread::Variant>( m_obscuranceVariantComboBox->currentIndex() ),
                                                           m_obscuranceDoublePrecisionRadioButton->isChecked(),
                                                           this );
        m_obscuranceMainThread->setVolume( m_volume->getVolume() );
        m_obscuranceMainThread->setTransferFunction( m_transferFunctionEditor->transferFunction() );

        m_obscurancePushButton->setText( tr("Cancel obscurance") );
        m_obscuranceProgressBar->setValue( 0 );
        connect( m_obscuranceMainThread, SIGNAL( progress(int) ), m_obscuranceProgressBar, SLOT( setValue(int) ) );
        connect( m_obscuranceMainThread, SIGNAL( computed() ), SLOT( endComputeObscurance() ) );
        m_obscuranceLoadPushButton->setEnabled( false );
        m_obscuranceSavePushButton->setEnabled( false );

        m_obscuranceMainThread->start();
    }
    else
    {
        m_obscuranceMainThread->stop();
        connect( m_obscuranceMainThread, SIGNAL( finished() ), SLOT( endCancelObscurance() ) );

        m_obscurancePushButton->setText( tr("Cancelling obscurance...") );
        m_obscurancePushButton->setEnabled( false );
    }
}


void QExperimental3DExtension::endComputeObscurance()
{
    m_computingObscurance = false;

    m_obscurance = m_obscuranceMainThread->getObscurance();
    m_obscurancePushButton->setText( tr("Compute obscurance") );
    m_obscuranceLoadPushButton->setEnabled( true );
    m_obscuranceSavePushButton->setEnabled( true );
    m_obscuranceCheckBox->setEnabled( true );
}


void QExperimental3DExtension::endCancelObscurance()
{
    m_computingObscurance = false;

    m_obscurancePushButton->setText( tr("Compute obscurance") );
    m_obscurancePushButton->setEnabled( true );
    m_obscuranceLoadPushButton->setEnabled( true );
}


void QExperimental3DExtension::loadObscurance()
{
    QString obscuranceFileName = getFileNameToLoad( Experimental3DSettings::ObscuranceDir, tr("Load obscurance"), tr("Data files (*.dat);;All files (*)") );

    if ( !obscuranceFileName.isNull() )
    {
        if ( m_obscuranceCheckBox->isChecked() )
        {
            m_obscuranceCheckBox->setChecked( false );
            this->render();
        }

        m_obscuranceCheckBox->setEnabled( false );

        delete m_obscurance;

        m_obscurance = new Obscurance( m_volume->getSize(), ObscuranceMainThread::hasColor( static_cast<ObscuranceMainThread::Variant>( m_obscuranceVariantComboBox->currentIndex() ) ),
                                       m_obscuranceDoublePrecisionRadioButton->isChecked() );
        bool ok = m_obscurance->load( obscuranceFileName );

        if ( ok )
        {
            m_obscuranceSavePushButton->setEnabled( true );
            m_obscuranceCheckBox->setEnabled( true );
        }
        else
        {
            m_obscuranceSavePushButton->setEnabled( false );
            if ( m_interactive ) QMessageBox::warning( this, tr("Can't load obscurance"), QString( tr("Can't load obscurance from file ") ) + obscuranceFileName );
        }
    }
}


void QExperimental3DExtension::saveObscurance()
{
    QString obscuranceFileName = getFileNameToSave( Experimental3DSettings::ObscuranceDir, tr("Save obscurance"), tr("Data files (*.dat);;All files (*)"), "dat" );

    if ( !obscuranceFileName.isNull() )
    {
        if ( !m_obscurance->save( obscuranceFileName ) )
        {
            if ( m_interactive ) QMessageBox::warning( this, tr("Can't save obscurance"), QString( tr("Can't save obscurance to file ") ) + obscuranceFileName );
        }
    }
}


void QExperimental3DExtension::setNumberOfSmiViewpoints( int numberOfViewpoints )
{
    m_smiViewpointSpinBox->setMaximum( numberOfViewpoints );
}


void QExperimental3DExtension::computeSmi()
{
    if ( m_smiDefaultAxisCheckBox->isChecked() )
    {
        VolumeReslicer volumeReslicer;
        volumeReslicer.setInput( m_volume->getImage() );
        volumeReslicer.noReslice();
        volumeReslicer.computeSmi();
    }
    else
    {
        QVector<Vector3> viewpoints = m_smiViewpointDistributionWidget->viewpoints();
        int i0, i1;

        if ( m_smiViewpointSpinBox->value() == 0 )  // tots
        {
            i0 = 0; i1 = viewpoints.size();
        }
        else    // un en concret
        {
            i0 = m_smiViewpointSpinBox->value() - 1; i1 = i0 + 1;
        }

        for ( int i = i0; i < i1; i++ )
        {
            const Vector3 &viewpoint = viewpoints.at( i );

            VolumeReslicer volumeReslicer( i + 1 );
            volumeReslicer.setInput( m_volume->getImage() );

            Vector3 position( viewpoint );
            Vector3 up( 0.0, 1.0, 0.0 );
            if ( qAbs( position.normalize() * up ) > 0.9 ) up = Vector3( 0.0, 0.0, 1.0 );
            volumeReslicer.setViewpoint( viewpoint, up );

            volumeReslicer.setSpacing( 1.0, 1.0, 1.0 );
            volumeReslicer.reslice();

            volumeReslicer.computeSmi();
        }
    }
}


void QExperimental3DExtension::computeSliceUnstabilities()
{
    if ( m_smiDefaultAxisCheckBox->isChecked() )
    {
        VolumeReslicer volumeReslicer;
        volumeReslicer.setInput( m_volume->getImage() );
        volumeReslicer.noReslice();
        volumeReslicer.computeSliceUnstabilities();
    }
    else
    {
        QVector<Vector3> viewpoints = m_smiViewpointDistributionWidget->viewpoints();
        int i0, i1;

        if ( m_smiViewpointSpinBox->value() == 0 )  // tots
        {
            i0 = 0; i1 = viewpoints.size();
        }
        else    // un en concret
        {
            i0 = m_smiViewpointSpinBox->value() - 1; i1 = i0 + 1;
        }

        for ( int i = i0; i < i1; i++ )
        {
            const Vector3 &viewpoint = viewpoints.at( i );

            VolumeReslicer volumeReslicer( i + 1 );
            volumeReslicer.setInput( m_volume->getImage() );

            Vector3 position( viewpoint );
            Vector3 up( 0.0, 1.0, 0.0 );
            if ( qAbs( position.normalize() * up ) > 0.9 ) up = Vector3( 0.0, 0.0, 1.0 );
            volumeReslicer.setViewpoint( viewpoint, up );

            volumeReslicer.setSpacing( 1.0, 1.0, 1.0 );
            volumeReslicer.reslice();

            volumeReslicer.computeSliceUnstabilities();
        }
    }
}


void QExperimental3DExtension::computePmi()
{
    if ( m_smiDefaultAxisCheckBox->isChecked() )
    {
        VolumeReslicer volumeReslicer;
        volumeReslicer.setInput( m_volume->getImage() );
        volumeReslicer.noReslice();
        volumeReslicer.computePmi();
    }
    else
    {
        QVector<Vector3> viewpoints = m_smiViewpointDistributionWidget->viewpoints();
        int i0, i1;

        if ( m_smiViewpointSpinBox->value() == 0 )  // tots
        {
            i0 = 0; i1 = viewpoints.size();
        }
        else    // un en concret
        {
            i0 = m_smiViewpointSpinBox->value() - 1; i1 = i0 + 1;
        }

        for ( int i = i0; i < i1; i++ )
        {
            const Vector3 &viewpoint = viewpoints.at( i );

            VolumeReslicer volumeReslicer( i + 1 );
            volumeReslicer.setInput( m_volume->getImage() );

            Vector3 position( viewpoint );
            Vector3 up( 0.0, 1.0, 0.0 );
            if ( qAbs( position.normalize() * up ) > 0.9 ) up = Vector3( 0.0, 0.0, 1.0 );
            volumeReslicer.setViewpoint( viewpoint, up );

            volumeReslicer.setSpacing( 1.0, 1.0, 1.0 );
            volumeReslicer.reslice();

            volumeReslicer.computePmi();
        }
    }
}


void QExperimental3DExtension::computePropertySaliencies()
{
    if ( m_smiDefaultAxisCheckBox->isChecked() )
    {
        VolumeReslicer volumeReslicer;
        volumeReslicer.setInput( m_volume->getImage() );
        volumeReslicer.noReslice();
        volumeReslicer.computePropertySaliencies();
    }
    else
    {
        QVector<Vector3> viewpoints = m_smiViewpointDistributionWidget->viewpoints();
        int i0, i1;

        if ( m_smiViewpointSpinBox->value() == 0 )  // tots
        {
            i0 = 0; i1 = viewpoints.size();
        }
        else    // un en concret
        {
            i0 = m_smiViewpointSpinBox->value() - 1; i1 = i0 + 1;
        }

        for ( int i = i0; i < i1; i++ )
        {
            const Vector3 &viewpoint = viewpoints.at( i );

            VolumeReslicer volumeReslicer( i + 1 );
            volumeReslicer.setInput( m_volume->getImage() );

            Vector3 position( viewpoint );
            Vector3 up( 0.0, 1.0, 0.0 );
            if ( qAbs( position.normalize() * up ) > 0.9 ) up = Vector3( 0.0, 0.0, 1.0 );
            volumeReslicer.setViewpoint( viewpoint, up );

            volumeReslicer.setSpacing( 1.0, 1.0, 1.0 );
            volumeReslicer.reslice();

            volumeReslicer.computePropertySaliencies();
        }
    }
}


void QExperimental3DExtension::computeSelectedVmi()
{
    // Què ha demanat l'usuari
    bool computeHV = m_computeHVCheckBox->isChecked();      // H(V)
    bool computeHVz = m_computeHVzCheckBox->isChecked();    // H(V|z)
    bool computeHZ = m_computeHZCheckBox->isChecked();      // H(Z)
    bool computeHZv = m_computeHZvCheckBox->isChecked();    // H(Z|v)
    bool computeHZV = m_computeHZVCheckBox->isChecked();    // H(Z|V)
    bool computeVmi = m_computeVmiCheckBox->isChecked();    // I₁(v;Z)
    bool computeVmi2 = m_computeVmi2CheckBox->isChecked();  // I₂(v;Z)
    bool computeVmi3 = m_computeVmi3CheckBox->isChecked();  // I₃(v;Z)
    bool computeMi = m_computeMiCheckBox->isChecked();
    bool computeViewpointUnstabilities = m_computeViewpointUnstabilitiesCheckBox->isChecked();
    bool computeVomi = m_computeVomiCheckBox->isChecked();      // I₁(z;V)
    bool computeVomi2 = m_computeVomi2CheckBox->isChecked();    // I₂(z;V)
    bool computeVomi3 = m_computeVomi3CheckBox->isChecked();    // I₃(z;V)
    bool computeViewpointVomi = m_computeViewpointVomiCheckBox->isChecked();    // INF
    bool computeViewpointVomi2 = m_computeViewpointVomi2CheckBox->isChecked();  // INF2
    bool computeColorVomi = m_computeColorVomiCheckBox->isChecked();
    bool computeEvmiOpacity = m_computeEvmiOpacityCheckBox->isChecked();
    bool computeEvmiVomi = m_computeEvmiVomiCheckBox->isChecked();
    bool computeBestViews = m_computeBestViewsCheckBox->isChecked();
    bool computeGuidedTour = m_computeGuidedTourCheckBox->isChecked();
    bool computeExploratoryTour = m_computeExploratoryTourCheckBox->isChecked();

    // Si no hi ha res a calcular marxem
    if (!computeHV && !computeHVz && !computeHZ && !computeHZv && !computeHZV && !computeVmi && !computeVmi2 && !computeVmi3 && !computeMi && !computeViewpointUnstabilities && !computeVomi && !computeVomi2
        && !computeVomi3 && !computeViewpointVomi && !computeViewpointVomi2 && !computeColorVomi && !computeEvmiOpacity && !computeEvmiVomi && !computeBestViews && !computeGuidedTour && !computeExploratoryTour)
        return;

    setCursor(QCursor(Qt::WaitCursor));

    // Obtenir direccions
    Vector3 position, focus, up;
    m_viewer->getCamera(position, focus, up);
    float distance = (position - focus).length();
    ViewpointGenerator viewpointGenerator = m_vmiViewpointDistributionWidget->viewpointGenerator(distance);

    // Viewpoint Information Channel
    ViewpointInformationChannel viewpointInformationChannel(viewpointGenerator, m_volume, m_viewer, m_transferFunctionEditor->transferFunction());

    // Paleta de colors per la color VoMI
    if (computeColorVomi) viewpointInformationChannel.setColorVomiPalette(m_colorVomiPalette);

    // Funció de transferència per l'EVMI amb opacitat
    if (computeEvmiOpacity)
    {
        if (m_computeEvmiOpacityUseOtherPushButton->isChecked()) viewpointInformationChannel.setEvmiOpacityTransferFunction(m_evmiOpacityTransferFunction);
        else viewpointInformationChannel.setEvmiOpacityTransferFunction(m_transferFunctionEditor->transferFunction());
    }

    // Paràmetres extres per calcular les millors vistes (els passem sempre perquè tinguin algun valor, per si s'ha de calcular el guided tour per exemple)
    viewpointInformationChannel.setBestViewsParameters(m_computeBestViewsNRadioButton->isChecked(), m_computeBestViewsNSpinBox->value(), m_computeBestViewsThresholdDoubleSpinBox->value());

    // Llindar per calcular l'exploratory tour
    viewpointInformationChannel.setExploratoryTourThreshold(m_computeExploratoryTourThresholdDoubleSpinBox->value());

    // Filtratge de punts de vista
    if (m_vmiOneViewpointCheckBox->isChecked())
    {
        int nViewpoints = m_vmiViewpointDistributionWidget->numberOfViewpoints();
        int selectedViewpoint = m_vmiOneViewpointSpinBox->value() - 1;

        QVector<bool> filter(nViewpoints);

        filter[selectedViewpoint] = true;

        QVector<int> neighbours = viewpointGenerator.neighbours(selectedViewpoint);
        for (int i = 0; i < neighbours.size(); i++) filter[neighbours.at(i)] = true;

        viewpointInformationChannel.filterViewpoints(filter);
    }

    connect(&viewpointInformationChannel, SIGNAL(totalProgressMaximum(int)), m_vmiTotalProgressBar, SLOT(setMaximum(int)));
    connect(&viewpointInformationChannel, SIGNAL(totalProgressMaximum(int)), m_vmiTotalProgressBar, SLOT(repaint()));  // no sé per què però cal això perquè s'actualitzi quan toca
    connect(&viewpointInformationChannel, SIGNAL(totalProgress(int)), m_vmiTotalProgressBar, SLOT(setValue(int)));
    connect(&viewpointInformationChannel, SIGNAL(partialProgress(int)), m_vmiProgressBar, SLOT(setValue(int)));

    QTime time;
    time.start();
    viewpointInformationChannel.compute(computeHV, computeHVz, computeHZ, computeHZv, computeHZV, computeVmi, computeVmi2, computeVmi3, computeMi, computeViewpointUnstabilities, computeVomi, computeVomi2,
                                        computeVomi3, computeViewpointVomi, computeViewpointVomi2, computeColorVomi, computeEvmiOpacity, computeEvmiVomi, computeBestViews, computeGuidedTour, computeExploratoryTour,
                                        m_vmiDisplayCheckBox->isChecked());
    int elapsed = time.elapsed();
    DEBUG_LOG(QString("Temps total de VOMI i altres: %1 s").arg(elapsed / 1000.0f));
    INFO_LOG(QString("Temps total de VOMI i altres: %1 s").arg(elapsed / 1000.0f));

    if (viewpointInformationChannel.hasViewedVolume())
    {
        m_viewedVolume = viewpointInformationChannel.viewedVolume();
        m_saveViewedVolumePushButton->setEnabled(true);
    }

    if (computeHV)
    {
        m_HV = viewpointInformationChannel.HV();
        m_saveHVPushButton->setEnabled(true);
    }

    if (computeHVz)
    {
        m_HVz = viewpointInformationChannel.HVz();
        m_saveHVzPushButton->setEnabled(true);
    }

    if (computeHZ)
    {
        m_HZ = viewpointInformationChannel.HZ();
        m_saveHZPushButton->setEnabled(true);
    }

    if (computeHZv)
    {
        m_HZv = viewpointInformationChannel.HZv();
        m_saveHZvPushButton->setEnabled(true);
    }

    if (computeHZV)
    {
        m_HZV = viewpointInformationChannel.HZV();
        m_saveHZVPushButton->setEnabled(true);
    }

    if (computeVmi)
    {
        m_vmi = viewpointInformationChannel.vmi();
        m_saveVmiPushButton->setEnabled(true);
    }

    if (computeVmi2)
    {
        m_vmi2 = viewpointInformationChannel.vmi2();
        m_saveVmi2PushButton->setEnabled(true);
    }

    if (computeVmi3)
    {
        m_vmi3 = viewpointInformationChannel.vmi3();
        m_saveVmi3PushButton->setEnabled(true);
    }

    if (computeMi)
    {
        m_mi = viewpointInformationChannel.mi();
        m_saveMiPushButton->setEnabled(true);
    }

    if (computeViewpointUnstabilities)
    {
        m_viewpointUnstabilities = viewpointInformationChannel.viewpointUnstabilities();
        m_saveViewpointUnstabilitiesPushButton->setEnabled(true);
    }

    if (computeVomi)
    {
        m_vomi = viewpointInformationChannel.vomi();
        m_minimumVomi = viewpointInformationChannel.minimumVomi();
        m_maximumVomi = viewpointInformationChannel.maximumVomi();
        DEBUG_LOG(QString("range vomi1 = [%1, %2]").arg(m_minimumVomi).arg(m_maximumVomi));
        m_baseVomiRadioButton->setEnabled(true);
        m_vomiCheckBox->setEnabled(true);
        m_vomiCoolWarmCheckBox->setEnabled(true);
        m_opacityLabel->setEnabled(true);
        m_opacityVomiCheckBox->setEnabled(true);
        m_saveVomiPushButton->setEnabled(true);
        m_vomiGradientPushButton->setEnabled(true);
    }

    if (computeVomi2)
    {
        m_vomi2 = viewpointInformationChannel.vomi2();
        m_minimumVomi2 = viewpointInformationChannel.minimumVomi2();
        m_maximumVomi2 = viewpointInformationChannel.maximumVomi2();
        DEBUG_LOG(QString("range vomi2 = [%1, %2]").arg(m_minimumVomi2).arg(m_maximumVomi2));
        m_baseVomiRadioButton->setEnabled(true);
        m_vomiCheckBox->setEnabled(true);
        //m_vomiCoolWarmCheckBox->setEnabled(true);   /// \todo vomi2
        //m_opacityLabel->setEnabled(true);
        //m_opacityVomiCheckBox->setEnabled(true);    /// \todo vomi2
        m_saveVomi2PushButton->setEnabled(true);
        m_vomi2GradientPushButton->setEnabled(true);
    }

    if (computeVomi3)
    {
        m_vomi3 = viewpointInformationChannel.vomi3();
        m_minimumVomi3 = viewpointInformationChannel.minimumVomi3();
        m_maximumVomi3 = viewpointInformationChannel.maximumVomi3();
        DEBUG_LOG(QString("range vomi3 = [%1, %2]").arg(m_minimumVomi3).arg(m_maximumVomi3));
        m_baseVomiRadioButton->setEnabled(true);
        m_vomiCheckBox->setEnabled(true);
        //m_vomiCoolWarmCheckBox->setEnabled(true);   /// \todo vomi3
        //m_opacityLabel->setEnabled(true);
        //m_opacityVomiCheckBox->setEnabled(true);    /// \todo vomi3
        m_saveVomi3PushButton->setEnabled(true);
        //m_vomiGradientPushButton->setEnabled(true); /// \todo vomi3
    }

    if (computeViewpointVomi)
    {
        m_viewpointVomi = viewpointInformationChannel.viewpointVomi();
        m_saveViewpointVomiPushButton->setEnabled(true);
    }

    if (computeViewpointVomi2)
    {
        m_viewpointVomi2 = viewpointInformationChannel.viewpointVomi2();
        m_saveViewpointVomi2PushButton->setEnabled(true);
    }

    if (computeColorVomi)
    {
        m_colorVomi = viewpointInformationChannel.colorVomi();
        m_maximumColorVomi = viewpointInformationChannel.maximumColorVomi();
        m_colorVomiCheckBox->setEnabled(true);
        m_saveColorVomiPushButton->setEnabled(true);
    }

    if (computeEvmiOpacity)
    {
        m_evmiOpacity = viewpointInformationChannel.evmiOpacity();
        m_saveEvmiOpacityPushButton->setEnabled(true);
    }

    if (computeEvmiVomi)
    {
        m_evmiVomi = viewpointInformationChannel.evmiVomi();
        m_saveEvmiVomiPushButton->setEnabled(true);
    }

    if (computeBestViews)
    {
        m_bestViews = viewpointInformationChannel.bestViews();
        m_saveBestViewsPushButton->setEnabled(true);
        m_tourBestViewsPushButton->setEnabled(true);
    }

    if (computeGuidedTour)
    {
        m_guidedTour = viewpointInformationChannel.guidedTour();
        m_saveGuidedTourPushButton->setEnabled(true);
        m_guidedTourPushButton->setEnabled(true);
    }

    if (computeExploratoryTour)
    {
        m_exploratoryTour = viewpointInformationChannel.exploratoryTour();
        m_saveExploratoryTourPushButton->setEnabled(true);
        m_exploratoryTourPushButton->setEnabled(true);
    }

    // Restaurem els paràmetres normals (en realitat només cal si es fa amb CPU)
    render();
    m_viewer->setCamera(position, focus, up);

    setCursor(QCursor(Qt::ArrowCursor));
}


void QExperimental3DExtension::computeSelectedVmii()
{
#ifndef CUDA_AVAILABLE
    QMessageBox::information( this, tr("Operation only available with CUDA"), "VMIi computations are only implemented in CUDA. Compile with CUDA support to use them." );
#else // CUDA_AVAILABLE
    // Què ha demanat l'usuari
    bool computePI = false;                                 // p(I)
    bool computeHI = m_computeHICheckBox->isChecked();      // H(I)
    bool computeHIv = m_computeHIvCheckBox->isChecked();    // H(I|v)
    bool computeHIV = m_computeHIVCheckBox->isChecked();    // H(I|V)
    bool computeJointEntropy = false;                       // H(V,I)
    bool computeVmii = m_computeVmiiCheckBox->isChecked();
    bool computeMii = m_computeMiiCheckBox->isChecked();
    bool computeViewpointUnstabilities = m_computeViewpointUnstabilitiesICheckBox->isChecked();
    bool computeImi = m_computeImiCheckBox->isChecked();
    bool computeIntensityClustering = m_computeIntensityClusteringCheckBox->isChecked();
    bool computeDkl_IV_W = false;                            // D_KL(I|V || W)

    // Si no hi ha res a calcular marxem
    if (computePI && !computeHI && !computeHIv && !computeHIV && !computeJointEntropy && !computeVmii && !computeMii && !computeViewpointUnstabilities && !computeImi && !computeIntensityClustering && !computeDkl_IV_W)
        return;

    setCursor(QCursor(Qt::WaitCursor));

    // Obtenir direccions
    Vector3 position, focus, up;
    m_viewer->getCamera( position, focus, up );
    float distance = (position - focus).length();
    ViewpointGenerator viewpointGenerator = m_vmiiViewpointDistributionWidget->viewpointGenerator(distance);

    // Viewpoint Intensity Information Channel
    ViewpointIntensityInformationChannel viewpointIntensityInformationChannel(viewpointGenerator, m_volume, m_viewer, m_transferFunctionEditor->transferFunction());

    // Nombre de clusters pel clustering d'intensitats
    viewpointIntensityInformationChannel.setIntensityClusteringNumberOfClusters(m_computeIntensityClusteringNumberOfClustersSpinBox->value());

    // Filtratge de punts de vista
    if (m_vmiiOneViewpointCheckBox->isChecked())
    {
        int nViewpoints = m_vmiiViewpointDistributionWidget->numberOfViewpoints();
        int selectedViewpoint = m_vmiiOneViewpointSpinBox->value() - 1;

        QVector<bool> filter(nViewpoints);

        filter[selectedViewpoint] = true;

        QVector<int> neighbours = viewpointGenerator.neighbours(selectedViewpoint);
        for (int i = 0; i < neighbours.size(); i++) filter[neighbours.at(i)] = true;

        viewpointIntensityInformationChannel.filterViewpoints(filter);
    }

    connect(&viewpointIntensityInformationChannel, SIGNAL(totalProgressMaximum(int)), m_vmiiTotalProgressBar, SLOT(setMaximum(int)));
    connect(&viewpointIntensityInformationChannel, SIGNAL(totalProgressMaximum(int)), m_vmiiTotalProgressBar, SLOT(repaint())); // no sé per què però cal això perquè s'actualitzi quan toca
    connect(&viewpointIntensityInformationChannel, SIGNAL(totalProgress(int)), m_vmiiTotalProgressBar, SLOT(setValue(int)));
    connect(&viewpointIntensityInformationChannel, SIGNAL(partialProgress(int)), m_vmiiProgressBar, SLOT(setValue(int)));

    QTime time;
    time.start();
    viewpointIntensityInformationChannel.compute(computePI, computeHI, computeHIv, computeHIV, computeJointEntropy, computeVmii, computeMii, computeViewpointUnstabilities, computeImi, computeIntensityClustering,
                                                 computeDkl_IV_W, m_vmiiDisplayCheckBox->isChecked());
    int elapsed = time.elapsed();
    DEBUG_LOG(QString("Temps total de VMIi i altres: %1 s").arg(elapsed / 1000.0f));
    INFO_LOG(QString("Temps total de VMIi i altres: %1 s").arg(elapsed / 1000.0f));

    if (viewpointIntensityInformationChannel.hasViewedVolume())
    {
        m_viewedVolumeI = viewpointIntensityInformationChannel.viewedVolume();
        m_saveViewedVolumeIPushButton->setEnabled(true);
    }

    if (computeHI)
    {
        m_HI = viewpointIntensityInformationChannel.HI();
        m_saveHIPushButton->setEnabled(true);
    }

    if (computeHIv)
    {
        m_HIv = viewpointIntensityInformationChannel.HIv();
        m_saveHIvPushButton->setEnabled(true);
    }

    if (computeHIV)
    {
        m_HIV = viewpointIntensityInformationChannel.HIV();
        m_saveHIVPushButton->setEnabled(true);
    }

    if (computeVmii)
    {
        m_vmii = viewpointIntensityInformationChannel.vmii();
        m_saveVmiiPushButton->setEnabled(true);
    }

    if (computeMii)
    {
        m_mii = viewpointIntensityInformationChannel.mii();
        m_saveMiiPushButton->setEnabled(true);
    }

    if (computeViewpointUnstabilities)
    {
        m_viewpointUnstabilitiesI = viewpointIntensityInformationChannel.viewpointUnstabilities();
        m_saveViewpointUnstabilitiesIPushButton->setEnabled(true);
    }

    if (computeImi)
    {
        m_imi = viewpointIntensityInformationChannel.imi();
        m_maximumImi = viewpointIntensityInformationChannel.maximumImi();
        m_baseImiRadioButton->setEnabled(true);
//        m_baseImiCoolWarmRadioButton->setEnabled(true);
//        m_imiCheckBox->setEnabled(true);
//        m_imiCoolWarmCheckBox->setEnabled(true);
//        m_opacityLabel->setEnabled(true);
//        m_opacityImiCheckBox->setEnabled(true);
        m_saveImiPushButton->setEnabled(true);
//        m_imiGradientPushButton->setEnabled(true);
        m_colorTransferFunctionFromImiPushButton->setEnabled(true);
        m_opacityTransferFunctionFromImiPushButton->setEnabled(true);
        m_transferFunctionFromImiPushButton->setEnabled(true);
    }

    if (computeIntensityClustering)
    {
        m_intensityClusters = viewpointIntensityInformationChannel.intensityClusters();
        fillWeigthsEditor();
    }

    setCursor(QCursor(Qt::ArrowCursor));
#endif // CUDA_AVAILABLE
}


void QExperimental3DExtension::tourBestViews()
{
    QList<Vector3> viewpoints;
    for ( int i = 0; i < m_bestViews.size(); i++ ) viewpoints << m_bestViews.at( i ).second;
    tour( viewpoints, m_tourSpeedDoubleSpinBox->value() );
}


void QExperimental3DExtension::guidedTour()
{
    QList<Vector3> viewpoints;
    for ( int i = 0; i < m_guidedTour.size(); i++ ) viewpoints << m_guidedTour.at( i ).second;
    tour( viewpoints, m_tourSpeedDoubleSpinBox->value() );
}


void QExperimental3DExtension::exploratoryTour()
{
    QList<Vector3> viewpoints;
    for ( int i = 0; i < m_exploratoryTour.size(); i++ ) viewpoints << m_exploratoryTour.at( i ).second;
    tour( viewpoints, m_tourSpeedDoubleSpinBox->value() );
}


void QExperimental3DExtension::computeVomiGradient()
{
    m_voxelSaliencies = m_volume->computeVomiGradient(m_vomi);
    m_maximumSaliency = 1.0f;
    m_baseVoxelSalienciesRadioButton->setEnabled(true);
    m_opacityLabel->setEnabled(true);
    m_opacitySaliencyCheckBox->setEnabled(true);
}


void QExperimental3DExtension::computeVomi2Gradient()
{
    m_voxelSaliencies = m_volume->computeVomiGradient(m_vomi2);
    m_maximumSaliency = 1.0f;
    m_baseVoxelSalienciesRadioButton->setEnabled(true);
    m_opacityLabel->setEnabled(true);
    m_opacitySaliencyCheckBox->setEnabled(true);
}


void QExperimental3DExtension::loadAndRunProgram()
{
    QString programFileName = getFileNameToLoad( Experimental3DSettings::ProgramDir, tr("Load program"), tr("Text files (*.txt);;All files (*)") );

    if ( programFileName.isNull() ) return;

    QFile programFile( programFileName );

    if ( !programFile.open( QFile::ReadOnly | QFile::Text ) )
    {
        DEBUG_LOG( QString( "No es pot llegir el fitxer " ) + programFileName );
        QMessageBox::warning( this, tr("Can't load program"), QString( tr("Can't load program from file ") ) + programFileName );
        return;
    }

    m_interactive = false;

    QTextStream in( &programFile );
    bool run = false;
    bool errors = false;

    for ( int i = 0; i < 2; i++ )
    {
        int lineNumber = 0;

        while ( !in.atEnd() && !( errors && run ) )
        {
            lineNumber++;

            QString line = in.readLine();
            QStringList words = line.split( ' ', QString::SkipEmptyParts );

            if ( words.isEmpty() ) continue;

            QString command = words.at( 0 );

            if ( command == "//" ) continue;
            else if ( command == "tab" )
            {
                if ( programCheckWordCount( lineNumber, line, 2 ) ) errors |= !programTab( lineNumber, line, run );
                else errors = true;
            }
            else if ( command == "rendering-interpolation" )
            {
                if ( programCheckWordCount( lineNumber, line, 2 ) ) errors |= !programRenderingInterpolation( lineNumber, line, run );
                else errors = true;
            }
            else if ( command == "rendering-gradientestimator" )
            {
                if ( programCheckWordCount( lineNumber, line, 2 ) ) errors |= !programRenderingGradientEstimator( lineNumber, line, run );
                else errors = true;
            }
            else if ( command == "rendering-baseshading" )
            {
                if ( programCheckWordCount( lineNumber, line, 2 ) ) errors |= !programRenderingBaseShading( lineNumber, line, run );
                else errors = true;
            }
            else if ( command == "rendering-check" || command == "rendering-uncheck" )
            {
                if ( programCheckWordCount( lineNumber, line, 2 ) ) errors |= !programRenderingCheckOrUncheck( lineNumber, line, run );
                else errors = true;
            }
            else if ( command == "tf-load" )
            {
                if ( programCheckWordCount( lineNumber, line, 2 ) )
                {
                    if ( run ) loadTransferFunction( words.at( 1 ) );
                }
                else errors = true;
            }
            else if ( command == "render" )
            {
                if ( run ) render();
            }
            else if ( command == "camera-load" )
            {
                if ( programCheckWordCount( lineNumber, line, 2 ) )
                {
                    if ( run ) loadCamera( words.at( 1 ) );
                }
                else errors = true;
            }
            else if ( command == "vmi-viewpoints" )
            {
                if ( programCheckWordCount( lineNumber, line, 3 ) ) errors |= !programVmiViewpoints( lineNumber, line, run );
                else errors = true;
            }
            else if ( command == "vmi-check" || command == "vmi-uncheck" )
            {
                if ( programCheckWordCount( lineNumber, line, 2 ) ) errors |= !programVmiCheckOrUncheck( lineNumber, line, run );
                else errors = true;
            }
            else if ( command == "vmi-run" )
            {
                if ( run ) computeSelectedVmi();
            }
            else if ( command == "vmi-vomigradient" )
            {
                if ( run )
                {
                    if ( m_vomiGradientPushButton->isEnabled() ) computeVomiGradient();
                    else
                    {
                        logProgramError( lineNumber, "No es pot calcular el gradient de la VoMI", line );
                        errors = true;
                    }
                }
            }
            else if ( command == "vmi-load" || command == "vmi-save" )
            {
                if ( programCheckWordCount( lineNumber, line, 3 ) ) errors |= !programVmiLoadOrSave( lineNumber, line, run );
                else errors = true;
            }
            else if ( command == "screenshot" )
            {
                if ( programCheckWordCount( lineNumber, line, 2 ) )
                {
                    if ( run ) m_viewer->screenshot( words.at( 1 ) );
                }
                else errors = true;
            }
            else
            {
                logProgramError( lineNumber, "Ordre desconeguda", line );
                errors = true;
            }
        }

        if ( errors )
        {
            QMessageBox::warning( this, tr("Errors in program"), tr("The errors have been written in the log.") );
            break;
        }
        else
        {
            in.seek( 0 );
            run = true;
        }
    }

    programFile.close();

    m_interactive = true;
}


void QExperimental3DExtension::logProgramError( int lineNumber, const QString &error, const QString &line ) const
{
    DEBUG_LOG( "[E3DP](" + QString::number( lineNumber ) + ") " + error + ": " + line );
    ERROR_LOG( "[E3DP](" + QString::number( lineNumber ) + ") " + error + ": " + line );
}


bool QExperimental3DExtension::programCheckWordCount( int lineNumber, const QString &line, int wordCount ) const
{
    if ( line.split( ' ', QString::SkipEmptyParts ).size() < wordCount )
    {
        logProgramError( lineNumber, "Falten paràmetres", line );
        return false;
    }
    else return true;
}


bool QExperimental3DExtension::programTab( int lineNumber, const QString &line, bool run )
{
    QString tab = line.split( ' ', QString::SkipEmptyParts ).at( 1 );

    if ( tab == "rendering" )
    {
        if ( run ) m_controlsTabWidget->setCurrentWidget( m_renderingTab );
    }
    else if ( tab == "camera" )
    {
        if ( run ) m_controlsTabWidget->setCurrentWidget( m_cameraTab );
    }
    else if ( tab == "obscurance" )
    {
        if ( run ) m_controlsTabWidget->setCurrentWidget( m_obscuranceTab );
    }
    else if ( tab == "smi" )
    {
        if ( run ) m_controlsTabWidget->setCurrentWidget( m_smiTab );
    }
    else if ( tab == "vmi" )
    {
        if ( run ) m_controlsTabWidget->setCurrentWidget( m_vmiTab );
    }
    else if ( tab == "program" )
    {
        if ( run ) m_controlsTabWidget->setCurrentWidget( m_programTab );
    }
    else
    {
        logProgramError( lineNumber, "Paràmetre/s incorrecte/s", line );
        return false;
    }

    return true;
}


bool QExperimental3DExtension::programRenderingInterpolation( int lineNumber, const QString &line, bool run )
{
    QString interpolation = line.split( ' ', QString::SkipEmptyParts ).at( 1 );

    if ( interpolation == "nn" )
    {
        if ( run ) m_interpolationComboBox->setCurrentIndex( 0 );
    }
    else if ( interpolation == "lic" )
    {
        if ( run ) m_interpolationComboBox->setCurrentIndex( 1 );
    }
    else if ( interpolation == "lci" )
    {
        if ( run ) m_interpolationComboBox->setCurrentIndex( 2 );
    }
    else
    {
        logProgramError( lineNumber, "Paràmetre/s incorrecte/s", line );
        return false;
    }

    return true;
}


bool QExperimental3DExtension::programRenderingGradientEstimator( int lineNumber, const QString &line, bool run )
{
    QString gradientEstimator = line.split( ' ', QString::SkipEmptyParts ).at( 1 );

    if ( gradientEstimator == "finitedifference" )
    {
        if ( run ) m_gradientEstimatorComboBox->setCurrentIndex( 0 );
    }
    else if ( gradientEstimator == "4dlr1" )
    {
        if ( run ) m_gradientEstimatorComboBox->setCurrentIndex( 1 );
    }
    else if ( gradientEstimator == "4dlr2" )
    {
        if ( run ) m_gradientEstimatorComboBox->setCurrentIndex( 2 );
    }
    else
    {
        logProgramError( lineNumber, "Paràmetre/s incorrecte/s", line );
        return false;
    }

    return true;
}


bool QExperimental3DExtension::programRenderingBaseShading( int lineNumber, const QString &line, bool run )
{
    QStringList words = line.split( ' ', QString::SkipEmptyParts );
    const QString &base = words.at( 1 );

    if ( base == "ambient" )
    {
        if ( run ) m_baseAmbientLightingRadioButton->setChecked( true );
    }
    else if ( base == "vomi" )
    {
        if ( run )
        {
            if ( m_baseVomiRadioButton->isEnabled() )
            {
                m_baseVomiRadioButton->setChecked( true );
                if ( words.size() > 2 ) m_baseVomiFactorDoubleSpinBox->setValue( words.at( 2 ).toDouble() );
            }
            else
            {
                logProgramError( lineNumber, "No es pot activar VoMI", line );
                return false;
            }
        }
    }
    else if ( base == "saliency" )
    {
        if ( run )
        {
            if ( m_baseVoxelSalienciesRadioButton->isEnabled() )
            {
                m_baseVoxelSalienciesRadioButton->setChecked( true );
                if ( words.size() > 2 ) m_baseVoxelSalienciesFactorDoubleSpinBox->setValue( words.at( 2 ).toDouble() );
            }
            else
            {
                logProgramError( lineNumber, "No es pot activar voxel saliencies", line );
                return false;
            }
        }
    }
    else
    {
        logProgramError( lineNumber, "Paràmetre/s incorrecte/s", line );
        return false;
    }

    return true;
}


bool QExperimental3DExtension::programRenderingCheckOrUncheck( int lineNumber, const QString &line, bool run )
{
    QStringList words = line.split( ' ', QString::SkipEmptyParts );
    bool check = words.at( 0 ) == "rendering-check";
    const QString &checkbox = words.at( 1 );

    if ( checkbox == "contour" )
    {
        if ( run )
        {
            m_contourCheckBox->setChecked( check );
            if ( check && words.size() > 2 ) m_contourDoubleSpinBox->setValue( words.at( 2 ).toDouble() );
        }
    }
    else if ( checkbox == "obscurance" )
    {
        if ( run )
        {
            if ( m_obscuranceCheckBox->isEnabled() )
            {
                m_obscuranceCheckBox->setChecked( check );

                if ( check && words.size() > 2 )
                {
                    m_obscuranceFactorDoubleSpinBox->setValue( words.at( 2 ).toDouble() );

                    if ( words.size() > 3 )
                    {
                        m_obscuranceLowFilterDoubleSpinBox->setValue( words.at( 3 ).toDouble() );
                        if ( words.size() > 4 ) m_obscuranceHighFilterDoubleSpinBox->setValue( words.at( 4 ).toDouble() );
                    }
                }
            }
            else
            {
                logProgramError( lineNumber, "No es pot activar les obscurances", line );
                return false;
            }
        }
    }
    else if ( checkbox == "vomi" )
    {
        if ( run )
        {
            if ( m_vomiCheckBox->isEnabled() )
            {
                m_vomiCheckBox->setChecked( check );
                if ( check && words.size() > 2 ) m_vomiFactorDoubleSpinBox->setValue( words.at( 2 ).toDouble() );
            }
            else
            {
                logProgramError( lineNumber, "No es pot activar VoMI", line );
                return false;
            }
        }
    }
    else if ( checkbox == "cvomi" )
    {
        if ( run )
        {
            if ( m_colorVomiCheckBox->isEnabled() )
            {
                m_colorVomiCheckBox->setChecked( check );
                if ( check && words.size() > 2 ) m_colorVomiFactorDoubleSpinBox->setValue( words.at( 2 ).toDouble() );
            }
            else
            {
                logProgramError( lineNumber, "No es pot activar color VoMI", line );
                return false;
            }
        }
    }
    else if ( checkbox == "opacity-vomi" )
    {
        if ( run )
        {
            if ( m_opacityVomiCheckBox->isEnabled() )
            {
                m_opacityVomiCheckBox->setChecked( check );
            }
            else
            {
                logProgramError( lineNumber, "No es pot activar l'opacitat de VoMI", line );
                return false;
            }
        }
    }
    else if ( checkbox == "opacity-saliency" )
    {
        if ( run )
        {
            if ( m_opacitySaliencyCheckBox->isEnabled() )
            {
                m_opacitySaliencyCheckBox->setChecked( check );
            }
            else
            {
                logProgramError( lineNumber, "No es pot activar l'opacitat de la saliency", line );
                return false;
            }
        }
    }
    else if ( checkbox == "celshading" )
    {
        if ( run )
        {
            m_celShadingCheckBox->setChecked( check );
            if ( check && words.size() > 2 ) m_celShadingQuantumsSpinBox->setValue( words.at( 2 ).toInt() );
        }
    }
    else
    {
        logProgramError( lineNumber, "Paràmetre/s incorrecte/s", line );
        return false;
    }

    return true;
}


bool QExperimental3DExtension::programVmiViewpoints( int lineNumber, const QString &line, bool run )
{
    QStringList words = line.split( ' ', QString::SkipEmptyParts );
    const QString &distribution = words.at( 1 );

    if ( distribution == "uni" )
    {
        int number = words.at( 2 ).toInt();

        switch ( number )
        {
            case 4: if ( run ) m_vmiViewpointDistributionWidget->setToUniform4(); break;
            case 6: if ( run ) m_vmiViewpointDistributionWidget->setToUniform6(); break;
            case 8: if ( run ) m_vmiViewpointDistributionWidget->setToUniform8(); break;
            case 12: if ( run ) m_vmiViewpointDistributionWidget->setToUniform12(); break;
            case 20: if ( run ) m_vmiViewpointDistributionWidget->setToUniform20(); break;
            default: logProgramError( lineNumber, "Nombre incorrecte de punts uniformes", line ); return false;
        }
    }
    else if ( distribution == "q-uni" )
    {
        if ( run ) m_vmiViewpointDistributionWidget->setToQuasiUniform( words.at( 2 ).toInt() );
    }
    else
    {
        logProgramError( lineNumber, "Paràmetre/s incorrecte/s", line );
        return false;
    }

    return true;
}


bool QExperimental3DExtension::programVmiCheckOrUncheck( int lineNumber, const QString &line, bool run )
{
    QStringList words = line.split( ' ', QString::SkipEmptyParts );
    bool check = words.at( 0 ) == "vmi-check";
    const QString &checkbox = words.at( 1 );

    if ( checkbox == "HZv" )
    {
        if ( run ) m_computeHZvCheckBox->setChecked( check );
    }
    else if ( checkbox == "vmi" )
    {
        if ( run ) m_computeVmiCheckBox->setChecked( check );
    }
    else if ( checkbox == "unstabilities" )
    {
        if ( run ) m_computeViewpointUnstabilitiesCheckBox->setChecked( check );
    }
    else if ( checkbox == "bestviews" )
    {
        if ( run ) m_computeBestViewsCheckBox->setChecked( check );

        if ( check && words.size() > 3 )
        {
            if ( words.at( 2 ) == "n" )
            {
                if ( run )
                {
                    m_computeBestViewsNRadioButton->setChecked( true );
                    m_computeBestViewsNSpinBox->setValue( words.at( 3 ).toInt() );
                }
            }
            else if ( words.at( 2 ) == "threshold" )
            {
                if ( run )
                {
                    m_computeBestViewsThresholdRadioButton->setChecked( true );
                    m_computeBestViewsThresholdDoubleSpinBox->setValue( words.at( 3 ).toDouble() );
                }
            }
            else
            {
                logProgramError( lineNumber, "Paràmetre/s incorrecte/s", line );
                return false;
            }
        }
    }
    else if ( checkbox == "guidedtour" )
    {
        if ( run ) m_computeGuidedTourCheckBox->setChecked( check );
    }
    else if ( checkbox == "vomi" )
    {
        if ( run ) m_computeVomiCheckBox->setChecked( check );
    }
    else if ( checkbox == "vvomi" )
    {
        if ( run ) m_computeViewpointVomiCheckBox->setChecked( check );
    }
    else if ( checkbox == "cvomi" )
    {
        if ( run )
        {
            m_computeColorVomiCheckBox->setChecked( check );
            if ( check && words.size() > 2 ) loadColorVomiPalette( words.at( 2 ) );
        }
    }
    else
    {
        logProgramError( lineNumber, "Paràmetre/s incorrecte/s", line );
        return false;
    }

    return true;
}


bool QExperimental3DExtension::programVmiLoadOrSave( int lineNumber, const QString &line, bool run )
{
    QStringList words = line.split( ' ', QString::SkipEmptyParts );
    bool load = words.at( 0 ) == "vmi-load";
    const QString &measure = words.at( 1 );
    const QString &fileName = words.at( 2 );

    if ( measure == "HZv" )
    {
        if ( run )
        {
            if ( load ) loadHZv( fileName );
            else
            {
                if ( m_saveHZvPushButton->isEnabled() ) saveHZv( fileName );
                else
                {
                    logProgramError( lineNumber, "No es pot desar l'entropia dels punts de vista", line );
                    return false;
                }
            }
        }
    }
    else if ( measure == "vmi" )
    {
        if ( run )
        {
            if ( load ) loadVmi( fileName );
            else
            {
                if ( m_saveVmiPushButton->isEnabled() ) saveVmi( fileName );
                else
                {
                    logProgramError( lineNumber, "No es pot desar la VMI", line );
                    return false;
                }
            }
        }
    }
    else if ( measure == "unstabilities" )
    {
        if ( run )
        {
            if ( load ) loadViewpointUnstabilities( fileName );
            else
            {
                if ( m_saveViewpointUnstabilitiesPushButton->isEnabled() ) saveViewpointUnstabilities( fileName );
                else
                {
                    logProgramError( lineNumber, "No es pot desar la inestabilitat dels punts de vista", line );
                    return false;
                }
            }
        }
    }
    else if ( measure == "bestviews" )
    {
        if ( run )
        {
            if ( load ) loadBestViews( fileName );
            else
            {
                if ( m_saveBestViewsPushButton->isEnabled() ) saveBestViews( fileName );
                else
                {
                    logProgramError( lineNumber, "No es pot desar les millors vistes", line );
                    return false;
                }
            }
        }
    }
    else if ( measure == "guidedtour" )
    {
        if ( run )
        {
            if ( load ) loadGuidedTour( fileName );
            else
            {
                if ( m_saveGuidedTourPushButton->isEnabled() ) saveGuidedTour( fileName );
                else
                {
                    logProgramError( lineNumber, "No es pot desar el guided tour", line );
                    return false;
                }
            }
        }
    }
    else if ( measure == "vomi" )
    {
        if ( run )
        {
            if ( load ) loadVomi( fileName );
            else
            {
                if ( m_saveVomiPushButton->isEnabled() ) saveVomi( fileName );
                else
                {
                    logProgramError( lineNumber, "No es pot desar la VoMI", line );
                    return false;
                }
            }
        }
    }
    else if ( measure == "vvomi" )
    {
        if ( run )
        {
            if ( load ) loadViewpointVomi( fileName );
            else
            {
                if ( m_saveViewpointVomiPushButton->isEnabled() ) saveViewpointVomi( fileName );
                else
                {
                    logProgramError( lineNumber, "No es pot desar la VoMI dels punts de vista", line );
                    return false;
                }
            }
        }
    }
    else if ( measure == "cvomi" )
    {
        if ( run )
        {
            if ( load ) loadColorVomi( fileName );
            else
            {
                if ( m_saveColorVomiPushButton->isEnabled() ) saveColorVomi( fileName );
                else
                {
                    logProgramError( lineNumber, "No es pot desar la color VoMI", line );
                    return false;
                }
            }
        }
    }
    else
    {
        logProgramError( lineNumber, "Paràmetre/s incorrecte/s", line );
        return false;
    }

    return true;
}


void QExperimental3DExtension::opacityVomiChecked( bool checked )
{
    if ( checked )
    {
        m_opacitySaliencyCheckBox->setChecked( false );
        m_opacityFilteringCheckBox->setChecked( false );
        m_opacityProbabilisticAmbientOcclusionCheckBox->setChecked( false );
        m_opacityLowLabel->setEnabled( true );
        m_opacityLowThresholdLabel->setEnabled( true );
        m_opacityLowThresholdDoubleSpinBox->setEnabled( true );
        m_opacityLowFactorLabel->setEnabled( true );
        m_opacityLowFactorDoubleSpinBox->setEnabled( true );
        m_opacityHighLabel->setEnabled( true );
        m_opacityHighThresholdLabel->setEnabled( true );
        m_opacityHighThresholdDoubleSpinBox->setEnabled( true );
        m_opacityHighFactorLabel->setEnabled( true );
        m_opacityHighFactorDoubleSpinBox->setEnabled( true );
    }
    else
    {
        m_opacityLowLabel->setEnabled( false );
        m_opacityLowThresholdLabel->setEnabled( false );
        m_opacityLowThresholdDoubleSpinBox->setEnabled( false );
        m_opacityLowFactorLabel->setEnabled( false );
        m_opacityLowFactorDoubleSpinBox->setEnabled( false );
        m_opacityHighLabel->setEnabled( false );
        m_opacityHighThresholdLabel->setEnabled( false );
        m_opacityHighThresholdDoubleSpinBox->setEnabled( false );
        m_opacityHighFactorLabel->setEnabled( false );
        m_opacityHighFactorDoubleSpinBox->setEnabled( false );
    }
}


void QExperimental3DExtension::opacitySaliencyChecked( bool checked )
{
    if ( checked )
    {
        m_opacityVomiCheckBox->setChecked( false );
        m_opacityFilteringCheckBox->setChecked( false );
        m_opacityProbabilisticAmbientOcclusionCheckBox->setChecked( false );
        m_opacityLowLabel->setEnabled( true );
        m_opacityLowThresholdLabel->setEnabled( true );
        m_opacityLowThresholdDoubleSpinBox->setEnabled( true );
        m_opacityLowFactorLabel->setEnabled( true );
        m_opacityLowFactorDoubleSpinBox->setEnabled( true );
        m_opacityHighLabel->setEnabled( true );
        m_opacityHighThresholdLabel->setEnabled( true );
        m_opacityHighThresholdDoubleSpinBox->setEnabled( true );
        m_opacityHighFactorLabel->setEnabled( true );
        m_opacityHighFactorDoubleSpinBox->setEnabled( true );
    }
    else
    {
        m_opacityLowLabel->setEnabled( false );
        m_opacityLowThresholdLabel->setEnabled( false );
        m_opacityLowThresholdDoubleSpinBox->setEnabled( false );
        m_opacityLowFactorLabel->setEnabled( false );
        m_opacityLowFactorDoubleSpinBox->setEnabled( false );
        m_opacityHighLabel->setEnabled( false );
        m_opacityHighThresholdLabel->setEnabled( false );
        m_opacityHighThresholdDoubleSpinBox->setEnabled( false );
        m_opacityHighFactorLabel->setEnabled( false );
        m_opacityHighFactorDoubleSpinBox->setEnabled( false );
    }
}


void QExperimental3DExtension::opacityFilteringChecked( bool checked )
{
    if ( checked )
    {
        m_opacityVomiCheckBox->setChecked( false );
        m_opacitySaliencyCheckBox->setChecked( false );
        m_opacityProbabilisticAmbientOcclusionCheckBox->setChecked( false );
        m_opacityLowLabel->setEnabled( true );
        m_opacityLowThresholdLabel->setEnabled( true );
        m_opacityLowThresholdDoubleSpinBox->setEnabled( true );
        m_opacityLowFactorLabel->setEnabled( true );
        m_opacityLowFactorDoubleSpinBox->setEnabled( true );
        m_opacityHighLabel->setEnabled( true );
        m_opacityHighThresholdLabel->setEnabled( true );
        m_opacityHighThresholdDoubleSpinBox->setEnabled( true );
        m_opacityHighFactorLabel->setEnabled( true );
        m_opacityHighFactorDoubleSpinBox->setEnabled( true );
    }
    else
    {
        m_opacityLowLabel->setEnabled( false );
        m_opacityLowThresholdLabel->setEnabled( false );
        m_opacityLowThresholdDoubleSpinBox->setEnabled( false );
        m_opacityLowFactorLabel->setEnabled( false );
        m_opacityLowFactorDoubleSpinBox->setEnabled( false );
        m_opacityHighLabel->setEnabled( false );
        m_opacityHighThresholdLabel->setEnabled( false );
        m_opacityHighThresholdDoubleSpinBox->setEnabled( false );
        m_opacityHighFactorLabel->setEnabled( false );
        m_opacityHighFactorDoubleSpinBox->setEnabled( false );
    }
}


void QExperimental3DExtension::opacityProbabilisticAmbientOcclusionChecked( bool checked )
{
    if ( checked )
    {
        m_opacityVomiCheckBox->setChecked( false );
        m_opacitySaliencyCheckBox->setChecked( false );
        m_opacityFilteringCheckBox->setChecked( false );
        m_opacityLowLabel->setEnabled( true );
        m_opacityLowThresholdLabel->setEnabled( true );
        m_opacityLowThresholdDoubleSpinBox->setEnabled( true );
        m_opacityLowFactorLabel->setEnabled( true );
        m_opacityLowFactorDoubleSpinBox->setEnabled( true );
        m_opacityHighLabel->setEnabled( true );
        m_opacityHighThresholdLabel->setEnabled( true );
        m_opacityHighThresholdDoubleSpinBox->setEnabled( true );
        m_opacityHighFactorLabel->setEnabled( true );
        m_opacityHighFactorDoubleSpinBox->setEnabled( true );
    }
    else
    {
        m_opacityLowLabel->setEnabled( false );
        m_opacityLowThresholdLabel->setEnabled( false );
        m_opacityLowThresholdDoubleSpinBox->setEnabled( false );
        m_opacityLowFactorLabel->setEnabled( false );
        m_opacityLowFactorDoubleSpinBox->setEnabled( false );
        m_opacityHighLabel->setEnabled( false );
        m_opacityHighThresholdLabel->setEnabled( false );
        m_opacityHighThresholdDoubleSpinBox->setEnabled( false );
        m_opacityHighFactorLabel->setEnabled( false );
        m_opacityHighFactorDoubleSpinBox->setEnabled( false );
    }
}


void QExperimental3DExtension::setVmiOneViewpointMaximum( int maximum )
{
    m_vmiOneViewpointSpinBox->setMaximum( maximum );
}


void QExperimental3DExtension::setVmiiOneViewpointMaximum( int maximum )
{
    m_vmiiOneViewpointSpinBox->setMaximum( maximum );
}


void QExperimental3DExtension::getFileNameToSaveTour()
{
    QString fileName = getFileNameToSave( Experimental3DSettings::TourDir, tr("Save tour"), tr("PNG files (*.png);;All files (*)"), "png" );

    if ( fileName.isNull() ) return;

    if ( !fileName.contains( "%1" ) )
    {
        int i = fileName.lastIndexOf( "." );
        fileName.insert( i, "%1" );
    }

    m_saveNextTourLineEdit->setText( fileName );
}


void QExperimental3DExtension::gaussianFilter()
{
    vtkImageCast *cast = vtkImageCast::New();
    cast->SetInput( m_volume->getImage() );
    cast->SetOutputScalarTypeToFloat();
    cast->Update();

#ifndef CUDA_AVAILABLE
    vtkImageGaussianSmooth *gaussian = vtkImageGaussianSmooth::New();
    gaussian->SetInput( cast->GetOutput() );
    gaussian->SetDimensionality( 3 );
    gaussian->SetRadiusFactor( m_filteringRadiusSpinBox->value() );
    gaussian->SetStandardDeviation( 1.0 );
    gaussian->Update();

    vtkImageMathematics *substract = vtkImageMathematics::New();
    substract->SetInput1( gaussian->GetOutput() );
    substract->SetInput2( cast->GetOutput() );
    substract->SetOperationToSubtract();
    substract->Update();

    vtkImageData *difference = substract->GetOutput();
    float *data = reinterpret_cast<float*>( difference->GetScalarPointer() );
    m_spatialImportanceFunction.resize( m_volume->getSize() );
    memcpy( m_spatialImportanceFunction.data(), data, m_spatialImportanceFunction.size() * sizeof(float) );
    double *range = difference->GetScalarRange();
    m_maximumSpatialImportanceFunction = qMax( qAbs( range[0] ), qAbs( range[1] ) );

    gaussian->Delete();
    substract->Delete();
#else // CUDA_AVAILABLE
    m_spatialImportanceFunction = cfGaussianDifference( cast->GetOutput(), m_filteringRadiusSpinBox->value() );
    int size = m_volume->getSize();
    m_maximumSpatialImportanceFunction = 0.0f;
    for ( int i = 0; i < size; i++ )
    {
        float f = qAbs( m_spatialImportanceFunction.at( i ) );
        if ( f > m_maximumSpatialImportanceFunction ) m_maximumSpatialImportanceFunction = f;
    }
#endif // CUDA_AVAILABLE

    m_baseFilteringAmbientOcclusionRadioButton->setEnabled( true );
    m_baseFilteringAmbientOcclusionStipplingRadioButton->setEnabled( true );
    m_filteringAmbientOcclusionCheckBox->setEnabled( true );
    m_opacityFilteringCheckBox->setEnabled( true );

    cast->Delete();
}


void QExperimental3DExtension::boxMeanFilter()
{
    vtkImageCast *cast = vtkImageCast::New();
    cast->SetInput( m_volume->getImage() );
    cast->SetOutputScalarTypeToFloat();
    cast->Update();

#ifndef CUDA_AVAILABLE
    QMessageBox::information( this, tr("Operation only available with CUDA"), "The box filter is only implemented in CUDA. Compile with CUDA support to use it." );
#else // CUDA_AVAILABLE
    m_spatialImportanceFunction = cfBoxMeanDifference( cast->GetOutput(), m_filteringRadiusSpinBox->value() );
    int size = m_volume->getSize();
    m_maximumSpatialImportanceFunction = 0.0f;
    for ( int i = 0; i < size; i++ )
    {
        float f = qAbs( m_spatialImportanceFunction.at( i ) );
        if ( f > m_maximumSpatialImportanceFunction ) m_maximumSpatialImportanceFunction = f;
    }
#endif // CUDA_AVAILABLE

    m_baseFilteringAmbientOcclusionRadioButton->setEnabled( true );
    m_baseFilteringAmbientOcclusionStipplingRadioButton->setEnabled( true );
    m_filteringAmbientOcclusionCheckBox->setEnabled( true );
    m_opacityFilteringCheckBox->setEnabled( true );

    cast->Delete();
}


void QExperimental3DExtension::probabilisticAmbientOcclusionGaussianChebychev()
{
    vtkImageCast *cast = vtkImageCast::New();
    cast->SetInput( m_volume->getImage() );
    cast->SetOutputScalarTypeToFloat();
    cast->Update();

#ifndef CUDA_AVAILABLE
    QMessageBox::information( this, tr("Operation only available with CUDA"), "The PAO Gaussian Chebychev is only implemented in CUDA. Compile with CUDA support to use it." );
#else // CUDA_AVAILABLE
    m_probabilisticAmbientOcclusion = cfProbabilisticAmbientOcclusionGaussianChebychev( cast->GetOutput(), m_probabilisticAmbientOcclusionRadiusSpinBox->value() );
#ifndef QT_NO_DEBUG
    int size = m_volume->getSize();
    for ( int i = 0; i < size; i++ )
    {
        if ( m_probabilisticAmbientOcclusion.at( i ) < 0.0f || m_probabilisticAmbientOcclusion.at( i ) > 1.0f )
        {
            DEBUG_LOG( QString( "pao[%1] = %2" ).arg( i ).arg( m_probabilisticAmbientOcclusion.at( i ) ) );
        }
    }
#endif // QT_NO_DEBUG
#endif // CUDA_AVAILABLE

    m_probabilisticAmbientOcclusionCheckBox->setEnabled( true );
    m_opacityProbabilisticAmbientOcclusionCheckBox->setEnabled( true );

    cast->Delete();
}


void QExperimental3DExtension::probabilisticAmbientOcclusionBoxMeanChebychev()
{
    vtkImageCast *cast = vtkImageCast::New();
    cast->SetInput( m_volume->getImage() );
    cast->SetOutputScalarTypeToFloat();
    cast->Update();

#ifndef CUDA_AVAILABLE
    QMessageBox::information( this, tr("Operation only available with CUDA"), "The PAO box mean Chebychev is only implemented in CUDA. Compile with CUDA support to use it." );
#else // CUDA_AVAILABLE
    m_probabilisticAmbientOcclusion = cfProbabilisticAmbientOcclusionBoxMeanChebychev( cast->GetOutput(), m_probabilisticAmbientOcclusionRadiusSpinBox->value() );
#ifndef QT_NO_DEBUG
    int size = m_volume->getSize();
    for ( int i = 0; i < size; i++ )
    {
        if ( m_probabilisticAmbientOcclusion.at( i ) < 0.0f || m_probabilisticAmbientOcclusion.at( i ) > 1.0f )
        {
            DEBUG_LOG( QString( "pao[%1] = %2" ).arg( i ).arg( m_probabilisticAmbientOcclusion.at( i ) ) );
        }
    }
#endif // QT_NO_DEBUG
#endif // CUDA_AVAILABLE

    m_probabilisticAmbientOcclusionCheckBox->setEnabled( true );
    m_opacityProbabilisticAmbientOcclusionCheckBox->setEnabled( true );

    cast->Delete();
}


void QExperimental3DExtension::probabilisticAmbientOcclusionGaussian()
{
    vtkImageCast *cast = vtkImageCast::New();
    cast->SetInput( m_volume->getImage() );
    cast->SetOutputScalarTypeToFloat();
    cast->Update();

#ifndef CUDA_AVAILABLE
    QMessageBox::information( this, tr("Operation only available with CUDA"), "The PAO Gaussian is only implemented in CUDA. Compile with CUDA support to use it." );
#else // CUDA_AVAILABLE
    m_probabilisticAmbientOcclusion = cfProbabilisticAmbientOcclusionGaussian( cast->GetOutput(), m_probabilisticAmbientOcclusionRadiusSpinBox->value() );
#ifndef QT_NO_DEBUG
    int size = m_volume->getSize();
    for ( int i = 0; i < size; i++ )
    {
        if ( m_probabilisticAmbientOcclusion.at( i ) < 0.0f || m_probabilisticAmbientOcclusion.at( i ) > 1.0f )
        {
            DEBUG_LOG( QString( "pao[%1] = %2" ).arg( i ).arg( m_probabilisticAmbientOcclusion.at( i ) ) );
        }
    }
#endif // QT_NO_DEBUG
#endif // CUDA_AVAILABLE

    m_probabilisticAmbientOcclusionCheckBox->setEnabled( true );
    m_opacityProbabilisticAmbientOcclusionCheckBox->setEnabled( true );

    cast->Delete();
}


void QExperimental3DExtension::probabilisticAmbientOcclusionCube()
{
    vtkImageCast *cast = vtkImageCast::New();
    cast->SetInput( m_volume->getImage() );
    cast->SetOutputScalarTypeToFloat();
    cast->Update();

#ifndef CUDA_AVAILABLE
    QMessageBox::information( this, tr("Operation only available with CUDA"), "The PAO cube is only implemented in CUDA. Compile with CUDA support to use it." );
#else // CUDA_AVAILABLE
    m_probabilisticAmbientOcclusion = cfProbabilisticAmbientOcclusionCube( cast->GetOutput(), m_probabilisticAmbientOcclusionRadiusSpinBox->value() );
#ifndef QT_NO_DEBUG
    int size = m_volume->getSize();
    for ( int i = 0; i < size; i++ )
    {
        if ( m_probabilisticAmbientOcclusion.at( i ) < 0.0f || m_probabilisticAmbientOcclusion.at( i ) > 1.0f )
        {
            DEBUG_LOG( QString( "pao[%1] = %2" ).arg( i ).arg( m_probabilisticAmbientOcclusion.at( i ) ) );
        }
    }
#endif // QT_NO_DEBUG
#endif // CUDA_AVAILABLE

    m_probabilisticAmbientOcclusionCheckBox->setEnabled( true );
    m_opacityProbabilisticAmbientOcclusionCheckBox->setEnabled( true );

    cast->Delete();
}


void QExperimental3DExtension::probabilisticAmbientOcclusionSphere()
{
    vtkImageCast *cast = vtkImageCast::New();
    cast->SetInput( m_volume->getImage() );
    cast->SetOutputScalarTypeToFloat();
    cast->Update();

#ifndef CUDA_AVAILABLE
    QMessageBox::information( this, tr("Operation only available with CUDA"), "The PAO sphere is only implemented in CUDA. Compile with CUDA support to use it." );
#else // CUDA_AVAILABLE
    m_probabilisticAmbientOcclusion = cfProbabilisticAmbientOcclusionSphere( cast->GetOutput(), m_probabilisticAmbientOcclusionRadiusSpinBox->value() );
#ifndef QT_NO_DEBUG
    int size = m_volume->getSize();
    for ( int i = 0; i < size; i++ )
    {
        if ( m_probabilisticAmbientOcclusion.at( i ) < 0.0f || m_probabilisticAmbientOcclusion.at( i ) > 1.0f )
        {
            DEBUG_LOG( QString( "pao[%1] = %2" ).arg( i ).arg( m_probabilisticAmbientOcclusion.at( i ) ) );
        }
    }
#endif // QT_NO_DEBUG
#endif // CUDA_AVAILABLE

    m_probabilisticAmbientOcclusionCheckBox->setEnabled( true );
    m_opacityProbabilisticAmbientOcclusionCheckBox->setEnabled( true );

    cast->Delete();
}


void QExperimental3DExtension::probabilisticAmbientOcclusionTangentSphereVariance()
{
    vtkImageCast *cast = vtkImageCast::New();
    cast->SetInput( m_volume->getImage() );
    cast->SetOutputScalarTypeToFloat();
    cast->Update();

#ifndef CUDA_AVAILABLE
    QMessageBox::information( this, tr("Operation only available with CUDA"), "The PAOTS variance is only implemented in CUDA. Compile with CUDA support to use it." );
#else // CUDA_AVAILABLE
    m_probabilisticAmbientOcclusion = cfProbabilisticAmbientOcclusionTangentSphereVariance( cast->GetOutput(), m_probabilisticAmbientOcclusionRadiusSpinBox->value() );
#ifndef QT_NO_DEBUG
    int size = m_volume->getSize();
    for ( int i = 0; i < size; i++ )
    {
        if ( m_probabilisticAmbientOcclusion.at( i ) < 0.0f || m_probabilisticAmbientOcclusion.at( i ) > 1.0f )
        {
            DEBUG_LOG( QString( "paots[%1] = %2" ).arg( i ).arg( m_probabilisticAmbientOcclusion.at( i ) ) );
        }
    }
#endif // QT_NO_DEBUG
#endif // CUDA_AVAILABLE

    m_probabilisticAmbientOcclusionCheckBox->setEnabled( true );
    m_opacityProbabilisticAmbientOcclusionCheckBox->setEnabled( true );

    cast->Delete();
}


void QExperimental3DExtension::probabilisticAmbientOcclusionTangentSphereCdf()
{
    vtkImageCast *cast = vtkImageCast::New();
    cast->SetInput( m_volume->getImage() );
    cast->SetOutputScalarTypeToFloat();
    cast->Update();

#ifndef CUDA_AVAILABLE
    QMessageBox::information( this, tr("Operation only available with CUDA"), "The PAOTS cdf is only implemented in CUDA. Compile with CUDA support to use it." );
#else // CUDA_AVAILABLE
    m_probabilisticAmbientOcclusion = cfProbabilisticAmbientOcclusionTangentSphereCdf( cast->GetOutput(), m_probabilisticAmbientOcclusionRadiusSpinBox->value() );
#ifndef QT_NO_DEBUG
    int size = m_volume->getSize();
    for ( int i = 0; i < size; i++ )
    {
        if ( m_probabilisticAmbientOcclusion.at( i ) < 0.0f || m_probabilisticAmbientOcclusion.at( i ) > 1.0f )
        {
            DEBUG_LOG( QString( "paots[%1] = %2" ).arg( i ).arg( m_probabilisticAmbientOcclusion.at( i ) ) );
        }
    }
#endif // QT_NO_DEBUG
#endif // CUDA_AVAILABLE

    m_probabilisticAmbientOcclusionCheckBox->setEnabled( true );
    m_opacityProbabilisticAmbientOcclusionCheckBox->setEnabled( true );

    cast->Delete();
}


void QExperimental3DExtension::probabilisticAmbientOcclusionTangentSphereGaussian()
{
    vtkImageCast *cast = vtkImageCast::New();
    cast->SetInput( m_volume->getImage() );
    cast->SetOutputScalarTypeToFloat();
    cast->Update();

#ifndef CUDA_AVAILABLE
    QMessageBox::information( this, tr("Operation only available with CUDA"), "The PAOTS Gaussian is only implemented in CUDA. Compile with CUDA support to use it." );
#else // CUDA_AVAILABLE
    m_probabilisticAmbientOcclusion = cfProbabilisticAmbientOcclusionTangentSphereGaussian( cast->GetOutput(), m_probabilisticAmbientOcclusionRadiusSpinBox->value() );
#ifndef QT_NO_DEBUG
    int size = m_volume->getSize();
    for ( int i = 0; i < size; i++ )
    {
        if ( m_probabilisticAmbientOcclusion.at( i ) < 0.0f || m_probabilisticAmbientOcclusion.at( i ) > 1.0f )
        {
            DEBUG_LOG( QString( "paots[%1] = %2" ).arg( i ).arg( m_probabilisticAmbientOcclusion.at( i ) ) );
        }
    }
#endif // QT_NO_DEBUG
#endif // CUDA_AVAILABLE

    m_probabilisticAmbientOcclusionCheckBox->setEnabled( true );
    m_opacityProbabilisticAmbientOcclusionCheckBox->setEnabled( true );

    cast->Delete();
}


void QExperimental3DExtension::probabilisticAmbientOcclusionGradient()
{
    m_voxelSaliencies = m_volume->computeVomiGradient( m_probabilisticAmbientOcclusion );
    m_maximumSaliency = 1.0f;
    m_baseVoxelSalienciesRadioButton->setEnabled( true );
    m_opacityLabel->setEnabled( true );
    m_opacitySaliencyCheckBox->setEnabled( true );
}


void QExperimental3DExtension::volumeVariance()
{
    vtkImageCast *cast = vtkImageCast::New();
    cast->SetInput( m_volume->getImage() );
    cast->SetOutputScalarTypeToFloat();
    cast->Update();

#ifndef CUDA_AVAILABLE
    QMessageBox::information( this, tr("Operation only available with CUDA"), "The volume variance is only implemented in CUDA. Compile with CUDA support to use it." );
#else // CUDA_AVAILABLE
    m_volumeVariance = cfVolumeVariance( cast->GetOutput(), m_probabilisticAmbientOcclusionRadiusSpinBox->value() );
#ifndef QT_NO_DEBUG
    int size = m_volume->getSize();
    for ( int i = 0; i < size; i++ )
    {
        if ( m_volumeVariance.at( i ) < 0.0f )
        {
            DEBUG_LOG( QString( "vv[%1] = %2" ).arg( i ).arg( m_volumeVariance.at( i ) ) );
        }
    }
#endif // QT_NO_DEBUG
#endif // CUDA_AVAILABLE

    m_opacityVarianceCheckBox->setEnabled( true );

    cast->Delete();
}


void QExperimental3DExtension::generateColorTransferFunctionFromImi()
{
    if ( m_imi.isEmpty() ) return;

    TransferFunction imiTransferFunction = m_transferFunctionEditor->transferFunction();
    imiTransferFunction.clearColor();
    imiTransferFunction.setName( "IMI color transfer function" );
    int nIntensities = m_volume->getRangeMax() + 1;

    for ( int i = 0; i < nIntensities; i++ )
    {
        float imi = m_imi.at( i ) / m_maximumImi;
        float red = imi > 0.8f ? 1.0f : imi > 0.6f ? 5.0f * ( imi - 0.6f ) : imi > 0.2f ? 0.0f : 1.0f - 5.0f * imi;
        float green = imi > 0.8f ? 1.0f - 5.0f * ( imi - 0.8f ) : imi > 0.4f ? 1.0f : imi > 0.2f ? 5.0f * ( imi - 0.2f ) : 0.0f;
        float blue = imi > 0.6f ? 0.0f : imi > 0.4f ? 1.0f - 5.0f * ( imi - 0.4f ) : 1.0f;
        imiTransferFunction.addPointToColor( i, QColor::fromRgbF( red, green, blue ) );
    }

    m_transferFunctionEditor->setTransferFunction( imiTransferFunction.simplify() );
    setTransferFunction();

}


void QExperimental3DExtension::generateOpacityTransferFunctionFromImi()
{
    if ( m_imi.isEmpty() ) return;

    TransferFunction imiTransferFunction = m_transferFunctionEditor->transferFunction();
    imiTransferFunction.clearOpacity();
    imiTransferFunction.setName( "IMI opacity transfer function" );
    int nIntensities = m_volume->getRangeMax() + 1;

    for ( int i = 0; i < nIntensities; i++ )
    {
        float imi = m_imi.at( i ) / m_maximumImi;
        imiTransferFunction.addPointToOpacity( i, imi );
    }

    m_transferFunctionEditor->setTransferFunction( imiTransferFunction.simplify() );
    setTransferFunction();

}


void QExperimental3DExtension::generateTransferFunctionFromImi()
{
    if ( m_imi.isEmpty() ) return;

    TransferFunction imiTransferFunction;
    imiTransferFunction.setName( "IMI transfer function" );
    int nIntensities = m_volume->getRangeMax() + 1;

    for ( int i = 0; i < nIntensities; i++ )
    {
        float imi = m_imi.at( i ) / m_maximumImi;
        float red = imi > 0.8f ? 1.0f : imi > 0.6f ? 5.0f * ( imi - 0.6f ) : imi > 0.2f ? 0.0f : 1.0f - 5.0f * imi;
        float green = imi > 0.8f ? 1.0f - 5.0f * ( imi - 0.8f ) : imi > 0.4f ? 1.0f : imi > 0.2f ? 5.0f * ( imi - 0.2f ) : 0.0f;
        float blue = imi > 0.6f ? 0.0f : imi > 0.4f ? 1.0f - 5.0f * ( imi - 0.4f ) : 1.0f;
        imiTransferFunction.addPoint( i, QColor::fromRgbF( red, green, blue, imi ) );
    }

    m_transferFunctionEditor->setTransferFunction( imiTransferFunction.simplify() );
    setTransferFunction();

}


void QExperimental3DExtension::generateTransferFunctionFromIntensityClusters()
{
    if (m_intensityClusters.isEmpty()) return;

    const TransferFunction &currentTransferFunction = m_transferFunctionEditor->transferFunction();
    TransferFunction clusteringTransferFunction;

    qsrand(m_randomSeedSpinBox->value());

    for (int i = 0; i < m_intensityClusters.size(); i++)
    {
        double x1 = m_intensityClusters[i].first();
        double x2 = m_intensityClusters[i].last();
        double x = (x1 + x2) / 2.0;

        QColor color(qrand() % 256, qrand() % 256, qrand() % 256);

        double opacity = 0.0;

        if (m_transferFunctionFromIntensityClusteringOpacityDefaultMinimumRadioButton->isChecked())
        {
            opacity = x1 / m_volume->getRangeMax();
        }
        else if (m_transferFunctionFromIntensityClusteringOpacityCurrentMinimumRadioButton->isChecked())    // l'opacitat serà la mínima del rang
        {
            QList<double> points = currentTransferFunction.getPointsInInterval(x1, x2);
            points.prepend(x1); points.append(x2);  // per si no hi són
            opacity = 1.0;

            for (int j = 0; j < points.size(); j++)
            {
                double a = currentTransferFunction.getOpacity(points.at(j));
                if (a < opacity) opacity = a;
            }
        }
        else if (m_transferFunctionFromIntensityClusteringOpacityCurrentMeanRadioButton->isChecked())   // l'opacitat serà la mitjana de l'interval
        {
            QList<double> points = currentTransferFunction.getPointsInInterval(x1, x2);
            points.prepend(x1); points.append(x2);  // per si no hi són
            opacity = 0.0;

            for (int j = 1; j < points.size(); j++)
            {
                double a1 = currentTransferFunction.getOpacity(points.at(j - 1));
                double a2 = currentTransferFunction.getOpacity(points.at(j));
                opacity += (points.at(j) - points.at(j - 1)) * (a1 + a2) / 2.0;
            }

            opacity /= x2 - x1;
        }

        if (m_transferFunctionFromIntensityClusteringTransferFunctionTypeCenterPointRadioButton->isChecked()) clusteringTransferFunction.addPoint(x, color, opacity);
        else if (m_transferFunctionFromIntensityClusteringTransferFunctionTypeRangeRadioButton->isChecked())
        {
            clusteringTransferFunction.addPoint(x1, color, opacity);
            clusteringTransferFunction.addPoint(x2, color, opacity);
        }
    }

    m_transferFunctionEditor->setTransferFunction(clusteringTransferFunction.simplify());
    setTransferFunction();
}


void QExperimental3DExtension::generateAndEvolveTransferFunctionFromIntensityClusters()
{
#ifndef CUDA_AVAILABLE
    QMessageBox::information(this, tr("Operation only available with CUDA"), "VMIi computations are only implemented in CUDA. Compile with CUDA support to use them.");
#else // CUDA_AVAILABLE
    if (m_intensityClusters.isEmpty()) return;

    setCursor(QCursor(Qt::WaitCursor));

    //generateTransferFunctionFromIntensityClusters();

    bool minimizeKullbackLeiblerDivergence = false;
    bool minimizeDkl_IV_W = true;
    bool maximizeHIV = false;
    bool maximizeMiiOverHI = false;
    bool minimizeMiiOverHI = false;
    bool maximizeMiiOverJointEntropy = false;
    bool minimizeMiiOverJointEntropy = false;

    const TransferFunction &weightsTransferFunction = m_geneticTransferFunctionFromIntensityClusteringWeightsEditor->transferFunction();
    QVector<float> weights(m_intensityClusters.size());

    if (minimizeKullbackLeiblerDivergence || minimizeDkl_IV_W)
    {
        float totalWeight = 0.0f;

        for (int i = 0; i < weights.size(); i++)
        {
            float weight = weightsTransferFunction.getOpacity(i);
            weights[i] = weight;
            totalWeight += weight;
        }

        DEBUG_LOG("pesos:");

        for (int i = 0; i < weights.size(); i++)
        {
            weights[i] /= totalWeight;
            DEBUG_LOG(QString("w(i%1) = %2").arg(i).arg(weights.at(i)));
        }
    }

    const double DeltaLimit1 = m_geneticTransferFunctionFromIntensityClusteringDelta1DoubleSpinBox->value();
    const double DeltaLimit2 = m_geneticTransferFunctionFromIntensityClusteringDelta2DoubleSpinBox->value();

    int iterations = m_geneticTransferFunctionFromIntensityClusteringIterationsSpinBox->value();
    TransferFunction bestTransferFunction = m_transferFunctionEditor->transferFunction();
    double best;
    QVector<float> bestPI;

    {
        // Obtenir direccions
        Vector3 position, focus, up;
        m_viewer->getCamera(position, focus, up);
        float distance = (position - focus).length();
        ViewpointGenerator viewpointGenerator;
        viewpointGenerator.setToUniform6(distance);

        // Viewpoint Intensity Information Channel
        ViewpointIntensityInformationChannel viewpointIntensityInformationChannel(viewpointGenerator, m_volume, m_viewer, bestTransferFunction);
        if (minimizeDkl_IV_W) viewpointIntensityInformationChannel.setWeights(weights);
        bool pI = minimizeKullbackLeiblerDivergence;
        bool HI = maximizeMiiOverHI || minimizeMiiOverHI;
        bool HIv = false;
        bool HIV = maximizeHIV;
        bool jointEntropy = maximizeMiiOverJointEntropy || minimizeMiiOverJointEntropy;
        bool vmii = false;
        bool mii = maximizeMiiOverHI || minimizeMiiOverHI || maximizeMiiOverJointEntropy || minimizeMiiOverJointEntropy;
        bool viewpointUnstabilities = false;
        bool imi = false;
        bool intensityClustering = false;
        bool Dkl_IV_W = minimizeDkl_IV_W;
        viewpointIntensityInformationChannel.compute(pI, HI, HIv, HIV, jointEntropy, vmii, mii, viewpointUnstabilities, imi, intensityClustering, Dkl_IV_W, false);

        if (minimizeKullbackLeiblerDivergence)
        {
            bestPI = viewpointIntensityInformationChannel.intensityProbabilities();
            best = InformationTheory::kullbackLeiblerDivergence(bestPI, weights, true);
        }
        if (minimizeDkl_IV_W)
        {
            best = viewpointIntensityInformationChannel.Dkl_IV_W();
        }
        if (maximizeHIV)
        {
            best = viewpointIntensityInformationChannel.HIV();
        }
        if (maximizeMiiOverHI || minimizeMiiOverHI)
        {
            double mii = viewpointIntensityInformationChannel.mii();
            double HI = viewpointIntensityInformationChannel.HI();
            best = mii / HI;
        }
        if (maximizeMiiOverJointEntropy || minimizeMiiOverJointEntropy)
        {
            double mii = viewpointIntensityInformationChannel.mii();
            double jointEntropy = viewpointIntensityInformationChannel.jointEntropy();
            best = mii / jointEntropy;
        }
    }

    qsrand(time(0));
    m_geneticTransferFunctionFromIntensityClusteringProgressBar->setValue(0);

    for (int i = 0; i < iterations; i++)
    {
        DEBUG_LOG(QString("------------------------------------- algorisme genètic, iteració %1/%2 --------------------------------").arg(i).arg(iterations));
        TransferFunction evolvedTransferFunction(bestTransferFunction);

        //for (int j = 0; j < m_intensityClusters.size(); j++)    // evolucionar-los tots
        for (int j = 1; j < m_intensityClusters.size(); j++)    // deixar el primer tal com està (a 0)
        {
            double x1 = m_intensityClusters[j].first();
            double x2 = m_intensityClusters[j].last();
            double x = (x1 + x2) / 2.0;
            double opacity = bestTransferFunction.getOpacity(x);
            double deltaMin, deltaMax;

            if (minimizeKullbackLeiblerDivergence)
            {
                double pi = bestPI.at(j);
                double w = weights.at(j);
                // heurística: si p(i) < w(i) donem més probabilitat que augmenti l'opacitat, i viceversa
                if (pi < w)
                {
                    deltaMin = -DeltaLimit1;
                    deltaMax = +DeltaLimit2;
                }
                else
                {
                    deltaMin = -DeltaLimit2;
                    deltaMax = +DeltaLimit1;
                }
            }
            if (minimizeDkl_IV_W || maximizeHIV || maximizeMiiOverHI || minimizeMiiOverHI || maximizeMiiOverJointEntropy || minimizeMiiOverJointEntropy)
            {
                deltaMin = -DeltaLimit2;
                deltaMax = +DeltaLimit2;
            }

            double deltaRange = deltaMax - deltaMin;
            double delta = deltaRange * qrand() / RAND_MAX + deltaMin;
            double newOpacity = qBound(0.0, opacity + delta, 1.0);
            DEBUG_LOG(QString("........................................ cluster %1: opacitat vella = %2, delta = %3%4, opacitat nova = %5").arg(j).arg(opacity).arg(delta > 0.0 ? "+" : "").arg(delta).arg(newOpacity));

            if (m_transferFunctionFromIntensityClusteringTransferFunctionTypeCenterPointRadioButton->isChecked())
            {
                evolvedTransferFunction.addPointToOpacity(x, newOpacity);
            }
            else if (m_transferFunctionFromIntensityClusteringTransferFunctionTypeRangeRadioButton->isChecked())
            {
                evolvedTransferFunction.addPointToOpacity(x1, newOpacity);
                evolvedTransferFunction.addPointToOpacity(x2, newOpacity);
            }
        }

        // Obtenir direccions
        Vector3 position, focus, up;
        m_viewer->getCamera(position, focus, up);
        float distance = (position - focus).length();
        ViewpointGenerator viewpointGenerator;
        viewpointGenerator.setToUniform6(distance);

        // Viewpoint Intensity Information Channel
        ViewpointIntensityInformationChannel viewpointIntensityInformationChannel(viewpointGenerator, m_volume, m_viewer, evolvedTransferFunction);
        if (minimizeDkl_IV_W) viewpointIntensityInformationChannel.setWeights(weights);
        bool pI = minimizeKullbackLeiblerDivergence;
        bool HI = maximizeMiiOverHI || minimizeMiiOverHI;
        bool HIv = false;
        bool HIV = maximizeHIV;
        bool jointEntropy = maximizeMiiOverJointEntropy || minimizeMiiOverJointEntropy;
        bool vmii = false;
        bool mii = maximizeMiiOverHI || minimizeMiiOverHI || maximizeMiiOverJointEntropy || minimizeMiiOverJointEntropy;
        bool viewpointUnstabilities = false;
        bool imi = false;
        bool intensityClustering = false;
        bool Dkl_IV_W = minimizeDkl_IV_W;
        viewpointIntensityInformationChannel.compute(pI, HI, HIv, HIV, jointEntropy, vmii, mii, viewpointUnstabilities, imi, intensityClustering, Dkl_IV_W, false);
        double evolved;
        bool accept;

        QVector<float> evolvedPI;
        if (minimizeKullbackLeiblerDivergence)
        {
            DEBUG_LOG("pesos:");
            for (int i = 0; i < weights.size(); i++)
            {
                DEBUG_LOG(QString("w(i%1) = %2").arg(i).arg(weights.at(i)));
            }
            evolvedPI = viewpointIntensityInformationChannel.intensityProbabilities();
            evolved = InformationTheory::kullbackLeiblerDivergence(evolvedPI, weights, true);
            DEBUG_LOG(QString(".......................................... distància mínima = %1, distància evolucionada = %2").arg(best).arg(evolved));
            accept = evolved < best;
        }
        if (minimizeDkl_IV_W)
        {
            evolved = viewpointIntensityInformationChannel.Dkl_IV_W();
            DEBUG_LOG(QString(".......................................... D_KL(I|V || W) mínima = %1, D_KL(I|V || W) evolucionada = %2").arg(best).arg(evolved));
            accept = evolved < best;
        }
        if (maximizeHIV)
        {
            evolved = viewpointIntensityInformationChannel.HIV();
            DEBUG_LOG(QString(".......................................... H(I|V) màxima = %1, H(I|V) evolucionada = %2").arg(best).arg(evolved));
            accept = evolved > best;
        }
        if (maximizeMiiOverHI || minimizeMiiOverHI)
        {
            double mii = viewpointIntensityInformationChannel.mii();
            double HI = viewpointIntensityInformationChannel.HI();
            evolved = mii / HI;
            if (maximizeMiiOverHI)
            {
                DEBUG_LOG(QString(".......................................... I(V;I)/H(I) màxima = %1, I(V;I)/H(I) evolucionada = %2").arg(best).arg(evolved));
                accept = evolved > best;
            }
            if (minimizeMiiOverHI)
            {
                DEBUG_LOG(QString(".......................................... I(V;I)/H(I) mínima = %1, I(V;I)/H(I) evolucionada = %2").arg(best).arg(evolved));
                accept = evolved < best;
            }
        }
        if (maximizeMiiOverJointEntropy || minimizeMiiOverJointEntropy)
        {
            double mii = viewpointIntensityInformationChannel.mii();
            double jointEntropy = viewpointIntensityInformationChannel.jointEntropy();
            evolved = mii / jointEntropy;
            if (maximizeMiiOverJointEntropy)
            {
                DEBUG_LOG(QString(".......................................... I(V;I)/H(V,I) màxima = %1, I(V;I)/H(V,I) evolucionada = %2").arg(best).arg(evolved));
                accept = evolved > best;
            }
            if (minimizeMiiOverJointEntropy)
            {
                DEBUG_LOG(QString(".......................................... I(V;I)/H(V,I) mínima = %1, I(V;I)/H(V,I) evolucionada = %2").arg(best).arg(evolved));
                accept = evolved < best;
            }
        }

        if (accept)
        {
            bestPI = evolvedPI;
            best = evolved;
            bestTransferFunction = evolvedTransferFunction;
            m_transferFunctionEditor->setTransferFunction(bestTransferFunction.simplify());
            setTransferFunction();
            DEBUG_LOG("......................................... acceptada");
        }
        else
        {
            DEBUG_LOG("......................................... rebutjada");
        }

        m_geneticTransferFunctionFromIntensityClusteringProgressBar->setValue(100 * i / iterations);
        m_geneticTransferFunctionFromIntensityClusteringProgressBar->repaint();
    }

    m_geneticTransferFunctionFromIntensityClusteringProgressBar->setValue(100);

    setCursor(QCursor(Qt::ArrowCursor));
#endif // CUDA_AVAILABLE
}


void QExperimental3DExtension::fineTuneGeneticTransferFunctionFromIntensityClusters()
{
#ifndef CUDA_AVAILABLE
    QMessageBox::information(this, tr("Operation only available with CUDA"), "VMIi computations are only implemented in CUDA. Compile with CUDA support to use them.");
#else // CUDA_AVAILABLE
    if (m_intensityClusters.isEmpty()) return;

    setCursor(QCursor(Qt::WaitCursor));

    //generateTransferFunctionFromIntensityClusters();

    bool minimizeKullbackLeiblerDivergence = false;
    bool minimizeDkl_IV_W = true;
    bool maximizeHIV = false;
    bool maximizeMiiOverHI = false;
    bool minimizeMiiOverHI = false;
    bool maximizeMiiOverJointEntropy = false;
    bool minimizeMiiOverJointEntropy = false;

    const TransferFunction &weightsTransferFunction = m_geneticTransferFunctionFromIntensityClusteringWeightsEditor->transferFunction();
    QVector<float> weights(m_intensityClusters.size());

    if (minimizeKullbackLeiblerDivergence || minimizeDkl_IV_W)
    {
        float totalWeight = 0.0f;

        for (int i = 0; i < weights.size(); i++)
        {
            float weight = weightsTransferFunction.getOpacity(i);
            weights[i] = weight;
            totalWeight += weight;
        }

        DEBUG_LOG("pesos:");

        for (int i = 0; i < weights.size(); i++)
        {
            weights[i] /= totalWeight;
            DEBUG_LOG(QString("w(i%1) = %2").arg(i).arg(weights.at(i)));
        }
    }

    int iterations = m_fineTuneGeneticTransferFunctionFromIntensityClusteringIterationsSpinBox->value();
    TransferFunction bestTransferFunction = m_transferFunctionEditor->transferFunction();
    double best;
    QVector<float> bestPI;

    {
        // Obtenir direccions
        Vector3 position, focus, up;
        m_viewer->getCamera(position, focus, up);
        float distance = (position - focus).length();
        ViewpointGenerator viewpointGenerator;
        viewpointGenerator.setToUniform6(distance);

        // Viewpoint Intensity Information Channel
        ViewpointIntensityInformationChannel viewpointIntensityInformationChannel(viewpointGenerator, m_volume, m_viewer, bestTransferFunction);
        if (minimizeDkl_IV_W) viewpointIntensityInformationChannel.setWeights(weights);
        bool pI = minimizeKullbackLeiblerDivergence;
        bool HI = maximizeMiiOverHI || minimizeMiiOverHI;
        bool HIv = false;
        bool HIV = maximizeHIV;
        bool jointEntropy = maximizeMiiOverJointEntropy || minimizeMiiOverJointEntropy;
        bool vmii = false;
        bool mii = maximizeMiiOverHI || minimizeMiiOverHI || maximizeMiiOverJointEntropy || minimizeMiiOverJointEntropy;
        bool viewpointUnstabilities = false;
        bool imi = false;
        bool intensityClustering = false;
        bool Dkl_IV_W = minimizeDkl_IV_W;
        viewpointIntensityInformationChannel.compute(pI, HI, HIv, HIV, jointEntropy, vmii, mii, viewpointUnstabilities, imi, intensityClustering, Dkl_IV_W, false);

        if (minimizeKullbackLeiblerDivergence)
        {
            bestPI = viewpointIntensityInformationChannel.intensityProbabilities();
            best = InformationTheory::kullbackLeiblerDivergence(bestPI, weights, true);
        }
        if (minimizeDkl_IV_W)
        {
            best = viewpointIntensityInformationChannel.Dkl_IV_W();
        }
        if (maximizeHIV)
        {
            best = viewpointIntensityInformationChannel.HIV();
        }
        if (maximizeMiiOverHI || minimizeMiiOverHI)
        {
            double mii = viewpointIntensityInformationChannel.mii();
            double HI = viewpointIntensityInformationChannel.HI();
            best = mii / HI;
        }
        if (maximizeMiiOverJointEntropy || minimizeMiiOverJointEntropy)
        {
            double mii = viewpointIntensityInformationChannel.mii();
            double jointEntropy = viewpointIntensityInformationChannel.jointEntropy();
            best = mii / jointEntropy;
        }
    }

    qsrand(time(0));
    m_fineTuneGeneticTransferFunctionFromIntensityClusteringProgressBar->setValue(0);

    for (int i = 0; i < iterations; i++)
    {
        DEBUG_LOG(QString("------------------------------------- fine-tune, iteració %1/%2 --------------------------------").arg(i).arg(iterations));

        int cluster = 1 + qrand() % (m_intensityClusters.size() - 1);
        double x1 = m_intensityClusters[cluster].first();
        double x2 = m_intensityClusters[cluster].last();
        double x = (x1 + x2) / 2.0;
        double opacity = bestTransferFunction.getOpacity(x);

        double base, step;
        int start, end = +10;

        switch (m_fineTuneGeneticTransferFunctionFromIntensityClusteringLevelComboBox->currentIndex())
        {
            case 0: base = 0.0; step = 0.1; start = 0; break;                                   // primer decimal
            case 1: base = qFloor(10.0 * opacity) / 10.0; step = 0.01; start = -10; break;      // segon decimal
            case 2: base = qFloor(100.0 * opacity) / 100.0; step = 0.001; start = -10; break;   // tercer decimal
        }

        for (int j = start; j <= end; j++)
        {
            TransferFunction fineTunedTransferFunction(bestTransferFunction);
            double newOpacity = qBound(0.0, base + j * step, 1.0);

            DEBUG_LOG(QString("........................................ cluster %1: opacitat vella = %2, opacitat nova = %3").arg(cluster).arg(opacity).arg(newOpacity));

            if (m_transferFunctionFromIntensityClusteringTransferFunctionTypeCenterPointRadioButton->isChecked())
            {
                fineTunedTransferFunction.addPointToOpacity(x, newOpacity);
            }
            else if (m_transferFunctionFromIntensityClusteringTransferFunctionTypeRangeRadioButton->isChecked())
            {
                fineTunedTransferFunction.addPointToOpacity(x1, newOpacity);
                fineTunedTransferFunction.addPointToOpacity(x2, newOpacity);
            }

            // Obtenir direccions
            Vector3 position, focus, up;
            m_viewer->getCamera(position, focus, up);
            float distance = (position - focus).length();
            ViewpointGenerator viewpointGenerator;
            viewpointGenerator.setToUniform6(distance);

            // Viewpoint Intensity Information Channel
            ViewpointIntensityInformationChannel viewpointIntensityInformationChannel(viewpointGenerator, m_volume, m_viewer, fineTunedTransferFunction);
            if (minimizeDkl_IV_W) viewpointIntensityInformationChannel.setWeights(weights);
            bool pI = minimizeKullbackLeiblerDivergence;
            bool HI = maximizeMiiOverHI || minimizeMiiOverHI;
            bool HIv = false;
            bool HIV = maximizeHIV;
            bool jointEntropy = maximizeMiiOverJointEntropy || minimizeMiiOverJointEntropy;
            bool vmii = false;
            bool mii = maximizeMiiOverHI || minimizeMiiOverHI || maximizeMiiOverJointEntropy || minimizeMiiOverJointEntropy;
            bool viewpointUnstabilities = false;
            bool imi = false;
            bool intensityClustering = false;
            bool Dkl_IV_W = minimizeDkl_IV_W;
            viewpointIntensityInformationChannel.compute(pI, HI, HIv, HIV, jointEntropy, vmii, mii, viewpointUnstabilities, imi, intensityClustering, Dkl_IV_W, false);
            double fineTuned;
            bool accept;

            QVector<float> fineTunedPI;
            if (minimizeKullbackLeiblerDivergence)
            {
                DEBUG_LOG("pesos:");
                for (int i = 0; i < weights.size(); i++)
                {
                    DEBUG_LOG(QString("w(i%1) = %2").arg(i).arg(weights.at(i)));
                }
                fineTunedPI = viewpointIntensityInformationChannel.intensityProbabilities();
                fineTuned = InformationTheory::kullbackLeiblerDivergence(fineTunedPI, weights, true);
                DEBUG_LOG(QString(".......................................... distància mínima = %1, distància ajustada = %2").arg(best).arg(fineTuned));
                accept = fineTuned < best;
            }
            if (minimizeDkl_IV_W)
            {
                fineTuned = viewpointIntensityInformationChannel.Dkl_IV_W();
                DEBUG_LOG(QString(".......................................... D_KL(I|V || W) mínima = %1, D_KL(I|V || W) ajustada = %2").arg(best).arg(fineTuned));
                accept = fineTuned < best;
            }
            if (maximizeHIV)
            {
                fineTuned = viewpointIntensityInformationChannel.HIV();
                DEBUG_LOG(QString(".......................................... H(I|V) màxima = %1, H(I|V) ajustada = %2").arg(best).arg(fineTuned));
                accept = fineTuned > best;
            }
            if (maximizeMiiOverHI || minimizeMiiOverHI)
            {
                double mii = viewpointIntensityInformationChannel.mii();
                double HI = viewpointIntensityInformationChannel.HI();
                fineTuned = mii / HI;
                if (maximizeMiiOverHI)
                {
                    DEBUG_LOG(QString(".......................................... I(V;I)/H(I) màxima = %1, I(V;I)/H(I) ajustada = %2").arg(best).arg(fineTuned));
                    accept = fineTuned > best;
                }
                if (minimizeMiiOverHI)
                {
                    DEBUG_LOG(QString(".......................................... I(V;I)/H(I) mínima = %1, I(V;I)/H(I) ajustada = %2").arg(best).arg(fineTuned));
                    accept = fineTuned < best;
                }
            }
            if (maximizeMiiOverJointEntropy || minimizeMiiOverJointEntropy)
            {
                double mii = viewpointIntensityInformationChannel.mii();
                double jointEntropy = viewpointIntensityInformationChannel.jointEntropy();
                fineTuned = mii / jointEntropy;
                if (maximizeMiiOverJointEntropy)
                {
                    DEBUG_LOG(QString(".......................................... I(V;I)/H(V,I) màxima = %1, I(V;I)/H(V,I) ajustada = %2").arg(best).arg(fineTuned));
                    accept = fineTuned > best;
                }
                if (minimizeMiiOverJointEntropy)
                {
                    DEBUG_LOG(QString(".......................................... I(V;I)/H(V,I) mínima = %1, I(V;I)/H(V,I) ajustada = %2").arg(best).arg(fineTuned));
                    accept = fineTuned < best;
                }
            }

            if (accept)
            {
                bestPI = fineTunedPI;
                best = fineTuned;
                bestTransferFunction = fineTunedTransferFunction;
                opacity = newOpacity;
                m_transferFunctionEditor->setTransferFunction(bestTransferFunction.simplify());
                setTransferFunction();
                DEBUG_LOG("......................................... acceptada");
            }
            else
            {
                DEBUG_LOG("......................................... rebutjada");
            }
        }

        m_fineTuneGeneticTransferFunctionFromIntensityClusteringProgressBar->setValue(100 * i / iterations);
        m_fineTuneGeneticTransferFunctionFromIntensityClusteringProgressBar->repaint();
    }

    m_fineTuneGeneticTransferFunctionFromIntensityClusteringProgressBar->setValue(100);

    setCursor(QCursor(Qt::ArrowCursor));
#endif // CUDA_AVAILABLE
}


void QExperimental3DExtension::generateInnernessProportionalOpacityTransferFunction()
{
    vtkImageData *image = m_volume->getImage();
    const unsigned short *data = reinterpret_cast<unsigned short*>(image->GetScalarPointer());
    int nVoxels = image->GetNumberOfPoints();
    int nIntensities = m_volume->getRangeMax() + 1;

    const float MissingData = -1.0;

    QVector<double> maximumDistanceToCenter(nIntensities, MissingData); // distància màxima al centre de cada intensitat
    double globalMaximumDistanceToCenter = 0.0;                         // distància màxima al centre de totes les intensitats

    double center[3];
    image->GetCenter(center);
    Vector3 c(center[0], center[1], center[2]);

    for (int i = 0; i < nVoxels; i++)
    {
        int intensity = data[i];
        double point[3];
        image->GetPoint(i, point);
        Vector3 p(point[0], point[1], point[2]);
        double d = (p - c).length();
        if (d > maximumDistanceToCenter.at(intensity)) maximumDistanceToCenter[intensity] = d;
        if (d > globalMaximumDistanceToCenter) globalMaximumDistanceToCenter = d;
    }

    const TransferFunction &currentTransferFunction = m_transferFunctionEditor->transferFunction();

    TransferFunction transferFunction(currentTransferFunction);
    transferFunction.setName(tr("Innerness-proportional opacity"));

    for (int i = 0; i < nIntensities; i++)
    {
        if (maximumDistanceToCenter.at(i) == MissingData) continue;
        // l'opacitat és inversament proporcional a la distància mitjana al centre, i es multiplica per l'opacitat definida a la funció actual
        transferFunction.addPointToOpacity(i, (1.0 - (maximumDistanceToCenter.at(i) / globalMaximumDistanceToCenter)) * currentTransferFunction.getOpacity(i));
    }

    m_transferFunctionEditor->setTransferFunction(transferFunction.simplify());
    setTransferFunction();
}


void QExperimental3DExtension::fillWeigthsEditor()
{
    if (m_intensityClusters.isEmpty()) return;
    if (m_geneticTransferFunctionFromIntensityClusteringWeightsManualRadioButton->isChecked()) return;

    m_geneticTransferFunctionFromIntensityClusteringWeightsEditor->setRange(0, m_intensityClusters.size() - 1);
    m_geneticTransferFunctionFromIntensityClusteringWeightsEditor->syncToMax();
    TransferFunction weightsTransferFunction;
    weightsTransferFunction.addPoint(0.0, QColor(0, 0, 0, 0));

    if (m_geneticTransferFunctionFromIntensityClusteringWeightsUniformRadioButton->isChecked()) // pesos uniformes
    {
        double weight = 1.0 / (m_intensityClusters.size() - 1);

        for (int i = 1; i < m_intensityClusters.size(); i++) weightsTransferFunction.addPointToOpacity(i, weight);
    }
    else if (m_geneticTransferFunctionFromIntensityClusteringWeightsVolumeDistributionRadioButton->isChecked()) // pesos segons la distribució al volum
    {
        const unsigned short *data = reinterpret_cast<unsigned short*>(m_volume->getImage()->GetScalarPointer());
        int size = m_volume->getImage()->GetNumberOfPoints();
        QVector<int> count(m_intensityClusters.size());
        int total = 0;

        for (int i = 0; i < size; i++)
        {
            if (data[i] > 0)
            {
                count[data[i]]++;
                total++;
            }
        }

        for (int i = 1; i < m_intensityClusters.size(); i++) weightsTransferFunction.addPointToOpacity(i, static_cast<double>(count.at(i)) / total);
    }

    m_geneticTransferFunctionFromIntensityClusteringWeightsEditor->setTransferFunction(weightsTransferFunction);
}


void QExperimental3DExtension::enableBaseVomi(bool on)
{
    m_baseVomi1RadioButton->setEnabled(!m_vomi.isEmpty() && on);
    m_baseVomi2RadioButton->setEnabled(!m_vomi2.isEmpty() && on);
    m_baseVomi3RadioButton->setEnabled(!m_vomi3.isEmpty() && on);
    m_baseVomiFactorLabel->setEnabled(on);
    m_baseVomiFactorDoubleSpinBox->setEnabled(on);
}


void QExperimental3DExtension::enableVomi(bool on)
{
    m_vomi1RadioButton->setEnabled(!m_vomi.isEmpty() && on);
    m_vomi2RadioButton->setEnabled(!m_vomi2.isEmpty() && on);
    m_vomi3RadioButton->setEnabled(!m_vomi3.isEmpty() && on);
    m_vomiFactorLabel->setEnabled(on);
    m_vomiFactorDoubleSpinBox->setEnabled(on);
}


} // namespace udg

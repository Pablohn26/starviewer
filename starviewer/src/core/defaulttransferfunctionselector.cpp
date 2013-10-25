#include "defaulttransferfunctionselector.h"

#include "logging.h"
#include "transferfunctionmodel.h"

namespace udg {

int DefaultTransferFunctionSelector::getDefaultTransferFunctionForPET(const TransferFunctionModel *model) const
{
    const QString TransferFunctionName = "Black & White Inverse";

    // In case of null or empty model, return -1
    if (!model || model->rowCount() == 0)
    {
        return -1;
    }

    // Find the transfer function by name
    TransferFunction transferFunction;
    transferFunction.setName(TransferFunctionName);
    int index = model->getIndexOf(transferFunction, true);

    if (index < 0)
    {
        WARN_LOG(QString("Can't find the default PET transfer function by name \"%1\".").arg(TransferFunctionName));
    }

    return index;
}

int DefaultTransferFunctionSelector::getDefaultTransferFunctionForPETCT(const TransferFunctionModel *model) const
{
    // In case of null or empty model, return -1
    if (!model || model->rowCount() == 0)
    {
        return -1;
    }

    // Return the first non-empty transfer function
    for (int i = 0; i < model->rowCount(); i++)
    {
        if (!model->getTransferFunction(i).isEmpty())
        {
            return i;
        }
    }

    // If we haven't found any non-empty transfer function, just return the first
    return 0;
}

}

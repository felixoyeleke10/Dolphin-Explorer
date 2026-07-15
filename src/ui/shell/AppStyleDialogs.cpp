#include "ui/shell/AppStylePrivate.h"

namespace dolphin::ui::detail {

QString qssDialogs()
{
    return qssDialogChrome()
         + qssDialogImport()
         + qssDialogContacts()
         + qssDialogProgress();
}

} // namespace dolphin::ui::detail

#include "lab02/page_provider.h"

#include "lab02/lab02_session.h"
#include "lab02/page_provider_http.h"

#include "sdkconfig.h"

#include <string.h>

bool lab02_page_provider_init(lab02_page_provider_t *pp)
{
    if (!pp) {
        return false;
    }
#if defined(CONFIG_LAB02_SERVER_URL)
    if (strlen(CONFIG_LAB02_SERVER_URL) > 0) {
        return lab02_page_provider_http_init(pp);
    }
#endif
    if (!lab02_page_provider_local_init(pp)) {
        return false;
    }
    lab02_session_attach_local(pp);
    return true;
}

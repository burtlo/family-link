#pragma once

namespace product_network
{

void start();
/* True after STA has received an IPv4 address (may still be true while muted). */
[[nodiscard]] bool sta_up();

} /* namespace product_network */

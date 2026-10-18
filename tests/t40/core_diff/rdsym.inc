
void *rd_sym(const char *n)
{
    if (!strcmp(n, "ispcore_sd")) return &ispcore_sd;
    if (!strcmp(n, "mscaler_s0")) return mscaler_s0;
    if (!strcmp(n, "mscaler_s1")) return mscaler_s1;
    if (!strcmp(n, "line_switch")) return &line_switch;
    if (!strcmp(n, "isp_memopt")) return &isp_memopt;
    return NULL;
}

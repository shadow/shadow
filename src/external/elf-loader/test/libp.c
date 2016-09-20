static int g_global = 0;

int libp_set_global (int new_value)
{
  int old = g_global;
  g_global = new_value;
  return old;
}



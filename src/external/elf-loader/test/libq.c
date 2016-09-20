static int g_global = 0;

int libq_set_global (int new_value)
{
  int old = g_global;
  g_global = new_value;
  return old;
}

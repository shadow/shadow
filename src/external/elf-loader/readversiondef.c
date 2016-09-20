#include <elf.h>
#include <link.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main (int argc, char *argv[])
{
  const char *filename = argv[1];
  int fd = open (filename, O_RDONLY);
  struct stat buf;
  fstat (fd, &buf);
  unsigned long file = (unsigned long)mmap (0, buf.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (file == (unsigned long)MAP_FAILED)
    {
      exit (1);
    }
  ElfW(Ehdr) *header = (ElfW(Ehdr) *)file;
  ElfW(Shdr) *sh = (ElfW(Shdr)*)(file + header->e_shoff);
  ElfW(Sym) *symtab = 0;
  unsigned long n_symtab = 0;
  ElfW(Half) *versym = 0;
  ElfW(Verdef) *verdef = 0;
  char *strtab = 0;
  int i;
  for (i = 0; i < header->e_shnum; i++)
    {
      if (sh[i].sh_type == SHT_DYNSYM)
	{
	  symtab = (ElfW(Sym)*)(file + sh[i].sh_offset);
	  n_symtab = sh[i].sh_size / sh[i].sh_entsize;
	}
      else if (sh[i].sh_type == SHT_STRTAB && strtab == 0)
	{
	  // XXX: should check the section name.
	  strtab = (char*)(file+sh[i].sh_offset);
	}
      else if (sh[i].sh_type == SHT_GNU_versym)
	{
	  versym = (ElfW(Half)*)(file+sh[i].sh_offset);
	}
      else if (sh[i].sh_type == SHT_GNU_verdef)
	{
	  verdef = (ElfW(Verdef)*)(file+sh[i].sh_offset);
	}
    }
  if (strtab == 0 || verdef == 0 || 
      symtab == 0 || n_symtab == 0 || versym == 0)
    {
      exit (3);
    }
  ElfW(Verdef) *cur, *prev;
  int local_passthru_printed = 0;
  for (prev = 0, cur = verdef;
       cur != prev; 
       prev = cur, cur = (ElfW(Verdef)*)(((unsigned long)cur)+cur->vd_next))
    {
      assert (cur->vd_version == 1);
      assert (cur->vd_cnt == 2 || cur->vd_cnt == 1);
      ElfW(Verdaux) *first = (ElfW(Verdaux)*)(((unsigned long)cur)+cur->vd_aux);
      if (cur->vd_flags & VER_FLG_BASE)
	{
	  continue;
	}
      printf ("%s {\n", strtab + first->vda_name);
      int has_one_symbol = 0;
      for (i = 0; i < n_symtab; i++)
	{
	  if (symtab[i].st_name == 0 || symtab[i].st_value == 0)
	    {
	      continue;
	    }
	  ElfW(Half) ver = versym[i];
	  if (cur->vd_ndx == ver)
	    {
	      if (!has_one_symbol)
		{
		  has_one_symbol = 1;
		  printf ("global:\n");
		}
	      printf ("\t%s;\n", strtab + symtab[i].st_name);
	    }
	}
      if (cur->vd_cnt == 1)
	{
	  if (!local_passthru_printed)
	    {
	      local_passthru_printed = 1;
	      printf ("local:*;\n};\n");
	    }
	  else
	    {
	      printf ("};\n");
	    }
	}
      else
	{
	  ElfW(Verdaux) *parent = (ElfW(Verdaux)*)(((unsigned long)first)+first->vda_next);
	  printf ("} %s;\n", strtab + parent->vda_name);
	}
    }
  return 0;
}

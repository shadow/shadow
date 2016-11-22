#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <elf.h>
#include <link.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>


int
main (int argc, char *argv[])
{
  const char *filename = argv[1];
  ElfW (Ehdr) header;
  int fd = open (filename, O_RDWR);

  ssize_t bytes_read = read (fd, &header, sizeof (header));
  if (bytes_read != sizeof (header))
    {
      return -1;
    }

  ElfW (Phdr) * ph = malloc (header.e_phnum * header.e_phentsize);
  if (ph == 0)
    {
      return -2;
    }
  if (lseek (fd, header.e_phoff, SEEK_SET) == -1)
    {
      return -3;
    }
  if (read (fd, ph, header.e_phnum * header.e_phentsize) !=
      header.e_phnum * header.e_phentsize)
    {
      return -4;
    }
  int i;
  for (i = 0; i < header.e_phnum; i++)
    {
      if (ph[i].p_type == PT_INTERP)
        {
          if (strlen (argv[2]) + 1 > ph[i].p_filesz)
            {
              return -5;
            }
          if (lseek (fd, ph[i].p_offset, SEEK_SET) == -1)
            {
              return -6;
            }
          char *interp = malloc (ph[i].p_filesz);
          memset (interp, 0, ph[i].p_filesz);
          memcpy (interp, argv[2], strlen (argv[2]));
          if (write (fd, argv[2], ph[i].p_filesz) != ph[i].p_filesz)
            {
              return -7;
            }
          if (lseek
              (fd, header.e_phoff + ((long) &ph[i].p_filesz - (long) ph),
               SEEK_SET) == -1)
            {
              return -8;
            }
          ElfW (Xword) filesz = strlen (argv[2]) + 1;
          if (write (fd, &filesz, sizeof (filesz)) != sizeof (filesz))
            {
              return -9;
            }
          if (lseek
              (fd, header.e_phoff + ((long) &ph[i].p_memsz - (long) ph),
               SEEK_SET) == -1)
            {
              return -10;
            }
          ElfW (Xword) memsz = strlen (argv[2]) + 1;
          if (write (fd, &memsz, sizeof (memsz)) != sizeof (memsz))
            {
              return -11;
            }
          return 0;
        }
    }


  return 0;
}

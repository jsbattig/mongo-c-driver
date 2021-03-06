#include <mongoc.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>

#include <mongoc-gridfs-cnv-file.h>

int main (int argc, char *argv[])
{
   mongoc_gridfs_t *gridfs;
   mongoc_gridfs_cnv_file_t *file;
   mongoc_gridfs_file_list_t *list;
   mongoc_gridfs_file_opt_t opt = { 0 };
   mongoc_client_t *client;
   bson_t query;
   bson_t child;
   bson_error_t error;
   ssize_t r;
   char buf[4096];
   mongoc_iovec_t iov;
   const char * filename;
   const char * command;
   const char pass[] = "my secret";

   if (argc < 2) {
      fprintf(stderr, "usage - %s command ...\n", argv[0]);
      return 1;
   }

   mongoc_init();

   iov.iov_base = (void *)buf;
   iov.iov_len = sizeof buf;

   /* connect to localhost client */
   client = mongoc_client_new ("mongodb://127.0.0.1:27017");
   assert(client);

   /* grab a gridfs handle in test prefixed by fs */
   gridfs = mongoc_client_get_gridfs (client, "test", "fs", &error);
   assert(gridfs);

   command = argv[1];
   filename = argv[2];

   if (strcmp(command, "read") == 0) {
      mongoc_stream_t *fstream;

      if (argc != 3) {
         fprintf(stderr, "usage - %s read filename\n", argv[0]);
         return 1;
      }
      file = mongoc_gridfs_find_one_cnv_by_filename(gridfs, filename, &error, MONGOC_CNV_UNCOMPRESS | MONGOC_CNV_DECRYPT);
      assert(file);
      mongoc_gridfs_cnv_file_set_aes_key_from_password (file, pass, sizeof pass);

      fstream = mongoc_stream_file_new_for_path (argv [2], O_CREAT | O_WRONLY | O_TRUNC, _S_IWRITE | _S_IREAD);
      assert (fstream);

      for (;;) {
         iov.iov_len = sizeof buf;
         r = mongoc_gridfs_cnv_file_readv (file, &iov, 1, -1, 0);

         assert (r >= 0);

         if (r == 0) {
            break;
         }
         iov.iov_len = (u_long)r;
         if (mongoc_stream_writev (fstream, &iov, 1, 0) == -1) {
            MONGOC_ERROR ("Failed to write to file. Exiting.\n");
            exit (1);
         }
      }

      mongoc_stream_destroy (fstream);
      mongoc_gridfs_cnv_file_destroy (file);
   } else if (strcmp(command, "list") == 0) {
      mongoc_gridfs_file_t *f;

      bson_init (&query);
      bson_append_document_begin (&query, "$orderby", -1, &child);
      bson_append_int32 (&child, "filename", -1, 1);
      bson_append_document_end (&query, &child);
      bson_append_document_begin (&query, "$query", -1, &child);
      bson_append_document_end (&query, &child);

      list = mongoc_gridfs_find (gridfs, &query);

      bson_destroy (&query);

      printf("file name\t"
             "is compressed\t"
             "is encrypted\t"
             "size\t"
             "compressed size\n");
      while ((f = mongoc_gridfs_file_list_next (list))) {
         file = mongoc_gridfs_cnv_file_from_file (f, MONGOC_CNV_NONE);

         printf("%s\t\t%c\t\t%c\t\t%" PRId64 "\t%" PRId64 "\n", 
                mongoc_gridfs_cnv_file_get_filename (file),
                mongoc_gridfs_cnv_file_is_compressed (file) ? 'Y' : 'N',
                mongoc_gridfs_cnv_file_is_encrypted (file) ? 'Y' : 'N',
                mongoc_gridfs_cnv_file_get_length (file),
                mongoc_gridfs_cnv_file_get_compressed_length (file));

         mongoc_gridfs_cnv_file_destroy (file);
      }

      mongoc_gridfs_file_list_destroy (list);
   } else if (strcmp(command, "write") == 0) {
      mongoc_stream_t *fstream;

      if (argc != 4) {
         fprintf(stderr, "usage - %s write filename input_file\n", argv[0]);
         return 1;
      }

      fstream = mongoc_stream_file_new_for_path (argv [3], O_RDONLY, 0);
      assert (fstream);

      opt.filename = filename;

      file = mongoc_gridfs_create_cnv_file (gridfs, &opt, MONGOC_CNV_ENCRYPT | MONGOC_CNV_COMPRESS);
      assert (file);
      if (!mongoc_gridfs_cnv_file_set_aes_key_from_password (file, pass, sizeof pass));

      for (;; ) {
        r = mongoc_stream_read (fstream, iov.iov_base, sizeof buf,
                                0, 0);

        assert (r >= 0);
        if (r == 0) {
            break;
        }
        iov.iov_len = (u_long)r;
        r = mongoc_gridfs_cnv_file_writev (file, &iov, 1, 0);
        assert (r > 0);
      }

      mongoc_stream_destroy (fstream);
      if (!mongoc_gridfs_cnv_file_save (file)) {
         fprintf(stderr, "failed to save cnv file");
         return 1;
      }
      mongoc_gridfs_cnv_file_destroy (file);
   } else {
      fprintf(stderr, "Unknown command");
      return 1;
   }

   mongoc_gridfs_destroy (gridfs);
   mongoc_client_destroy (client);

   mongoc_cleanup ();

   return 0;
}

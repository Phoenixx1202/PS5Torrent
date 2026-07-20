#include "torrent.h"
#include "bencode.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

torrent_t *torrent_parse(const unsigned char *data, size_t data_len)
{
    if (!data || data_len == 0) return NULL;

    bcode_node_t *root = bcode_parse(data, data_len);
    if (!root || root->type != BCODE_DICT) {
        bcode_free(root);
        return NULL;
    }

    torrent_t *t = calloc(1, sizeof(torrent_t));
    if (!t) { bcode_free(root); return NULL; }

    /* Parse announce */
    bcode_node_t *announce = bcode_dict_get(root, "announce");
    if (announce) {
        size_t alen;
        const unsigned char *astr = bcode_str_val(announce, &alen);
        if (astr) {
            t->announce = malloc(alen + 1);
            if (t->announce) {
                memcpy(t->announce, astr, alen);
                t->announce[alen] = '\0';
                t->announce_len = alen;
            }
        }
    }

    /* Parse announce-list (multi-tracker) */
    bcode_node_t *alist = bcode_dict_get(root, "announce-list");
    if (alist && alist->type == BCODE_LIST) {
        /* Count trackers */
        size_t count = 0;
        for (size_t i = 0; i < alist->child_count; i++) {
            if (alist->children[i]->type == BCODE_LIST)
                count += alist->children[i]->child_count;
        }

        if (count > 0) {
            t->announce_list = calloc(count, sizeof(char *));
            size_t idx = 0;
            for (size_t i = 0; i < alist->child_count && idx < count; i++) {
                if (alist->children[i]->type != BCODE_LIST) continue;
                for (size_t j = 0; j < alist->children[i]->child_count && idx < count; j++) {
                    size_t url_len;
                    const unsigned char *url = bcode_str_val(
                        alist->children[i]->children[j], &url_len);
                    if (url) {
                        t->announce_list[idx] = malloc(url_len + 1);
                        if (t->announce_list[idx]) {
                            memcpy(t->announce_list[idx], url, url_len);
                            t->announce_list[idx][url_len] = '\0';
                            idx++;
                        }
                    }
                }
            }
            t->announce_list_count = idx;
        }
    }

    /* Get info dictionary and compute info hash */
    bcode_node_t *info = bcode_dict_get(root, "info");
    if (!info) { torrent_free(t); bcode_free(root); return NULL; }

    /* Re-encode the info dictionary exactly as it was in the torrent file */
    t->info_dict_raw = bcode_encode(info, &t->info_dict_len);
    if (!t->info_dict_raw) { torrent_free(t); bcode_free(root); return NULL; }

    /* Compute info hash = SHA-1 of bencoded info dict */
    sha1_hash(t->info_dict_raw, t->info_dict_len, t->info_hash);

    /* Parse name */
    bcode_node_t *name = bcode_dict_get(info, "name");
    if (name) {
        size_t nlen;
        const unsigned char *nstr = bcode_str_val(name, &nlen);
        if (nstr) {
            t->name = malloc(nlen + 1);
            if (t->name) {
                memcpy(t->name, nstr, nlen);
                t->name[nlen] = '\0';
                t->name_len = nlen;
            }
        }
    }

    /* Parse piece length */
    bcode_node_t *pl = bcode_dict_get(info, "piece length");
    t->piece_length = pl ? bcode_int_val(pl) : 0;

    /* Parse pieces (concatenated SHA-1 hashes) */
    bcode_node_t *pieces = bcode_dict_get(info, "pieces");
    if (pieces && pieces->type == BCODE_STR) {
        t->pieces = malloc(pieces->str_len);
        if (t->pieces) {
            memcpy(t->pieces, pieces->str_val, pieces->str_len);
            t->pieces_len = pieces->str_len;
            t->num_pieces = pieces->str_len / SHA1_DIGEST_SIZE;
        }
    }

    /* Check if multi-file */
    bcode_node_t *files = bcode_dict_get(info, "files");
    if (files && files->type == BCODE_LIST) {
        /* Multi-file torrent */
        t->is_multi_file = 1;
        t->num_files = files->child_count;
        if (t->num_files > MAX_TORRENT_FILES)
            t->num_files = MAX_TORRENT_FILES;

        t->files = calloc(t->num_files, sizeof(torrent_file_t));

        for (size_t i = 0; i < t->num_files; i++) {
            bcode_node_t *fentry = files->children[i];
            if (!fentry || fentry->type != BCODE_DICT) continue;

            bcode_node_t *flen = bcode_dict_get(fentry, "length");
            t->files[i].length = flen ? bcode_int_val(flen) : 0;
            t->total_size += t->files[i].length;

            bcode_node_t *fpath = bcode_dict_get(fentry, "path");
            if (fpath && fpath->type == BCODE_LIST) {
                /* Build path from path components */
                size_t path_total = 0;
                for (size_t j = 0; j < fpath->child_count; j++) {
                    if (j > 0) path_total++; // separator
                    size_t clen;
                    const unsigned char *c = bcode_str_val(
                        fpath->children[j], &clen);
                    if (c) path_total += clen;
                }

                t->files[i].path = malloc(path_total + 1);
                if (t->files[i].path) {
                    size_t pos = 0;
                    for (size_t j = 0; j < fpath->child_count; j++) {
                        if (j > 0) t->files[i].path[pos++] = '/';
                        size_t clen;
                        const unsigned char *c = bcode_str_val(
                            fpath->children[j], &clen);
                        if (c) {
                            memcpy(t->files[i].path + pos, c, clen);
                            pos += clen;
                        }
                    }
                    t->files[i].path[pos] = '\0';
                    t->files[i].path_len = pos;
                }
            }
        }
    } else {
        /* Single file torrent */
        t->is_multi_file = 0;
        bcode_node_t *flen = bcode_dict_get(info, "length");
        t->length = flen ? bcode_int_val(flen) : 0;
        t->total_size = t->length;

        /* File name */
        bcode_node_t *fname = bcode_dict_get(info, "name");
        if (fname && fname->type == BCODE_STR) {
            t->file_name = malloc(fname->str_len + 1);
            if (t->file_name) {
                memcpy(t->file_name, fname->str_val, fname->str_len);
                t->file_name[fname->str_len] = '\0';
            }
        }
    }

    bcode_free(root);
    return t;
}

torrent_t *torrent_parse_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0) { fclose(f); return NULL; }

    unsigned char *data = malloc((size_t)fsize);
    if (!data) { fclose(f); return NULL; }

    size_t nread = fread(data, 1, (size_t)fsize, f);
    fclose(f);

    if (nread != (size_t)fsize) { free(data); return NULL; }

    torrent_t *t = torrent_parse(data, (size_t)fsize);
    free(data);
    return t;
}

void torrent_info_hash_str(const torrent_t *torrent, char *buf)
{
    if (!torrent || !buf) return;
    for (int i = 0; i < SHA1_DIGEST_SIZE; i++) {
        sprintf(buf + (i * 2), "%02x", torrent->info_hash[i]);
    }
    buf[40] = '\0';
}

void torrent_free(torrent_t *torrent)
{
    if (!torrent) return;

    free(torrent->announce);

    if (torrent->announce_list) {
        for (size_t i = 0; i < torrent->announce_list_count; i++)
            free(torrent->announce_list[i]);
        free(torrent->announce_list);
    }

    free(torrent->info_dict_raw);
    free(torrent->name);
    free(torrent->pieces);
    free(torrent->file_name);

    if (torrent->files) {
        for (size_t i = 0; i < torrent->num_files; i++)
            free(torrent->files[i].path);
        free(torrent->files);
    }

    free(torrent);
}

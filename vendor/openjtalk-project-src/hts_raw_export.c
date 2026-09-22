#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "HTS_engine.h"
#include "HTS_hidden.h"
#include "hts_walloc.h"
#include "hts_raw_export.h"

typedef struct HtsModelInspectStats {
   size_t question_count;
   size_t question_pattern_count;
   size_t question_string_bytes;
   size_t tree_count;
   size_t tree_pattern_count;
   size_t tree_pattern_string_bytes;
   size_t node_count;
   size_t pdf_count;
   size_t pdf_value_count;
   size_t pdf_float_bytes;
   size_t npdf_table_bytes;
   size_t pointer_table_bytes;
   size_t current_struct_bytes;
   size_t current_estimated_bytes;
   size_t flash32_estimated_bytes;
} HtsModelInspectStats;

typedef struct HtsRawSectionHeader {
   char name[16];
   uint32_t element_size;
   uint32_t count;
   uint32_t byte_size;
   uint32_t reserved;
} HtsRawSectionHeader;

typedef struct HtsRawVoiceHeader {
   char magic[8];
   uint32_t version;
   uint32_t section_count;
   uint32_t sampling_frequency;
   uint32_t frame_period;
   uint32_t num_voices;
   uint32_t num_states;
   uint32_t num_streams;
   uint32_t model_count;
   uint32_t window_count;
} HtsRawVoiceHeader;

typedef struct HtsRawModelRecord {
   uint32_t kind;
   uint32_t voice_index;
   uint32_t stream_index;
   uint32_t vector_length;
   uint32_t num_windows;
   uint32_t is_msd;
   uint32_t ntree;
   uint32_t pdf_count;
   uint32_t pdf_value_count;
   uint32_t pdf_float_offset;
   uint32_t npdf_offset;
   uint32_t npdf_count;
   uint32_t question_offset;
   uint32_t question_count;
   uint32_t qpattern_offset;
   uint32_t qpattern_count;
   uint32_t tree_offset;
   uint32_t tree_count;
   uint32_t tpattern_offset;
   uint32_t tpattern_count;
   uint32_t node_offset;
   uint32_t node_count;
} HtsRawModelRecord;

typedef struct HtsRawWindowRecord {
   uint32_t stream_index;
   uint32_t window_index;
   int32_t left_width;
   int32_t right_width;
   uint32_t coefficient_offset;
   uint32_t coefficient_count;
} HtsRawWindowRecord;

typedef struct HtsRawQuestionRecord {
   uint32_t name_offset;
   uint32_t pattern_offset;
   uint32_t pattern_count;
} HtsRawQuestionRecord;

typedef struct HtsRawPatternRecord {
   uint32_t string_offset;
} HtsRawPatternRecord;

typedef struct HtsRawTreeRecord {
   uint32_t state;
   uint32_t pattern_offset;
   uint32_t pattern_count;
   uint32_t root_node_index;
} HtsRawTreeRecord;

typedef struct HtsRawNodeRecord {
   int32_t source_index;
   uint32_t question_index;
   uint32_t yes_index;
   uint32_t no_index;
   uint32_t pdf_index;
} HtsRawNodeRecord;

typedef struct HtsRawBuffer {
   unsigned char *data;
   size_t size;
   size_t capacity;
} HtsRawBuffer;

static void raw_buffer_free(HtsRawBuffer *buffer)
{
   free(buffer->data);
   memset(buffer, 0, sizeof(*buffer));
}

static int raw_buffer_reserve(HtsRawBuffer *buffer, size_t additional)
{
   size_t required;
   size_t capacity;
   unsigned char *data;

   if (additional > (size_t) -1 - buffer->size)
      return 0;

   required = buffer->size + additional;
   if (required <= buffer->capacity)
      return 1;

   capacity = buffer->capacity != 0 ? buffer->capacity : 4096;
   while (capacity < required) {
      if (capacity > (size_t) -1 / 2)
         return 0;
      capacity *= 2;
   }

   data = (unsigned char *) realloc(buffer->data, capacity);
   if (data == NULL)
      return 0;

   buffer->data = data;
   buffer->capacity = capacity;
   return 1;
}

static int raw_buffer_append(HtsRawBuffer *buffer, const void *data, size_t size)
{
   if (size == 0)
      return 1;

   if (!raw_buffer_reserve(buffer, size))
      return 0;

   memcpy(buffer->data + buffer->size, data, size);
   buffer->size += size;
   return 1;
}

static int append_u32(HtsRawBuffer *buffer, uint32_t value)
{
   return raw_buffer_append(buffer, &value, sizeof(value));
}

static int checked_u32(size_t value, uint32_t *out_value)
{
   if (value > UINT32_MAX)
      return 0;
   *out_value = (uint32_t) value;
   return 1;
}

static int append_string(HtsRawBuffer *strings, const char *string, uint32_t *offset)
{
   size_t length;

   if (!checked_u32(strings->size, offset))
      return 0;

   if (string == NULL)
      string = "";

   length = strlen(string) + 1;
   return raw_buffer_append(strings, string, length);
}

static int write_exact(FILE *fp, const void *data, size_t size)
{
   return size == 0 || fwrite(data, 1, size, fp) == size;
}

static size_t align4(size_t value)
{
   return (value + 3u) & ~(size_t) 3u;
}

static int write_padding4(FILE *fp, size_t payload_size)
{
   static const unsigned char zero_padding[3] = { 0u, 0u, 0u };
   size_t padding_size = align4(payload_size) - payload_size;

   return write_exact(fp, zero_padding, padding_size);
}

static int write_section(FILE *fp, const char *name, uint32_t element_size, uint32_t count, const HtsRawBuffer *buffer)
{
   HtsRawSectionHeader header;

   memset(&header, 0, sizeof(header));
   strncpy(header.name, name, sizeof(header.name) - 1);
   header.element_size = element_size;
   header.count = count;
   if (!checked_u32(buffer->size, &header.byte_size))
      return 0;

   return write_exact(fp, &header, sizeof(header)) &&
      write_exact(fp, buffer->data, buffer->size) &&
      write_padding4(fp, buffer->size);
}

static void print_memory_stats(const char *label)
{
   HTS_WallocStats stats;

   if (!hts_walloc_get_stats(&stats)) {
      fprintf(stderr, "memory_stats[%s]: unavailable\n", label);
      return;
   }

   fprintf(stderr,
           "memory_stats[%s]: total=%zu managed=%zu used=%zu free=%zu max_free_block=%zu used_blocks=%zu free_blocks=%zu block_header=%zu used_block_headers=%zu peak_used=%zu peak_used_blocks=%zu\n",
           label,
           stats.total_bytes,
           stats.managed_bytes,
           stats.used_bytes,
           stats.free_bytes,
           stats.max_free_block_bytes,
           stats.used_blocks,
           stats.free_blocks,
           stats.block_header_bytes,
           stats.used_block_header_bytes,
           stats.peak_used_bytes,
           stats.peak_used_blocks);
}

static size_t count_patterns(const HTS_Pattern *pattern)
{
   size_t count = 0;

   for (; pattern != NULL; pattern = pattern->next)
      count++;

   return count;
}

static size_t inspect_pattern_list(const HTS_Pattern *pattern, size_t *pattern_count)
{
   size_t string_bytes = 0;

   *pattern_count = 0;

   for (; pattern != NULL; pattern = pattern->next) {
      (*pattern_count)++;
      if (pattern->string != NULL)
         string_bytes += strlen(pattern->string) + 1;
   }

   return string_bytes;
}

static void inspect_question_list(const HTS_Question *question, size_t *question_count, size_t *pattern_count)
{
   size_t string_bytes;

   *question_count = 0;
   *pattern_count = 0;

   for (; question != NULL; question = question->next) {
      size_t local_pattern_count = 0;
      (*question_count)++;
      string_bytes = inspect_pattern_list(question->head, &local_pattern_count);
      (void) string_bytes;
      *pattern_count += local_pattern_count;
   }
}

static size_t inspect_question_string_bytes(const HTS_Question *question)
{
   size_t string_bytes = 0;

   for (; question != NULL; question = question->next) {
      size_t ignored_count = 0;
      if (question->string != NULL)
         string_bytes += strlen(question->string) + 1;
      string_bytes += inspect_pattern_list(question->head, &ignored_count);
   }

   return string_bytes;
}

static size_t count_nodes(const HTS_Node *node)
{
   if (node == NULL)
      return 0;

   return 1 + count_nodes(node->yes) + count_nodes(node->no);
}

static size_t count_trees(const HTS_Tree *tree, size_t *tree_pattern_count, size_t *node_count)
{
   size_t tree_count = 0;

   *tree_pattern_count = 0;
   *node_count = 0;

   for (; tree != NULL; tree = tree->next) {
      tree_count++;
      *tree_pattern_count += count_patterns(tree->head);
      *node_count += count_nodes(tree->root);
   }

   return tree_count;
}

static size_t count_pdfs(const HTS_Model *model)
{
   size_t i;
   size_t count = 0;

   if (model == NULL || model->npdf == NULL)
      return 0;

   for (i = 2; i <= model->ntree + 1; i++)
      count += model->npdf[i];

   return count;
}

static size_t count_pdf_values(const HTS_Model *model)
{
   size_t pdf_count;
   size_t values_per_pdf;

   if (model == NULL)
      return 0;

   pdf_count = count_pdfs(model);
   values_per_pdf = model->vector_length * model->num_windows * 2;
   if (model->is_msd)
      values_per_pdf++;

   return pdf_count * values_per_pdf;
}

static void inspect_model_stats(const HTS_Model *model, HtsModelInspectStats *stats)
{
   size_t tree_pattern_string_bytes = 0;
   const HTS_Tree *tree;
   size_t i;

   memset(stats, 0, sizeof(*stats));

   if (model == NULL || (model->ntree == 0 && model->vector_length == 0 && model->tree == NULL))
      return;

   inspect_question_list(model->question, &stats->question_count, &stats->question_pattern_count);
   stats->question_string_bytes = inspect_question_string_bytes(model->question);
   stats->tree_count = count_trees(model->tree, &stats->tree_pattern_count, &stats->node_count);

   for (tree = model->tree; tree != NULL; tree = tree->next) {
      size_t ignored_count = 0;
      tree_pattern_string_bytes += inspect_pattern_list(tree->head, &ignored_count);
   }
   stats->tree_pattern_string_bytes = tree_pattern_string_bytes;

   stats->pdf_count = count_pdfs(model);
   stats->pdf_value_count = count_pdf_values(model);
   stats->pdf_float_bytes = stats->pdf_value_count * sizeof(float);
   stats->npdf_table_bytes = model->ntree * sizeof(size_t);
   stats->pointer_table_bytes = model->ntree * sizeof(float **);

   if (model->npdf != NULL) {
      for (i = 2; i <= model->ntree + 1; i++)
         stats->pointer_table_bytes += model->npdf[i] * sizeof(float *);
   }

   stats->current_struct_bytes =
      sizeof(HTS_Model) +
      stats->question_count * sizeof(HTS_Question) +
      stats->question_pattern_count * sizeof(HTS_Pattern) +
      stats->tree_count * sizeof(HTS_Tree) +
      stats->tree_pattern_count * sizeof(HTS_Pattern) +
      stats->node_count * sizeof(HTS_Node);

   stats->current_estimated_bytes =
      stats->current_struct_bytes +
      stats->question_string_bytes +
      stats->tree_pattern_string_bytes +
      stats->npdf_table_bytes +
      stats->pointer_table_bytes +
      stats->pdf_float_bytes;

   stats->flash32_estimated_bytes =
      32 +                                      /* compact model header */
      stats->question_count * 12 +             /* name offset, first pattern, count */
      stats->question_pattern_count * 8 +      /* string offset, next/index */
      stats->tree_count * 16 +                 /* pattern offset/count, root, state */
      stats->tree_pattern_count * 8 +          /* string offset, next/index */
      stats->node_count * 20 +                 /* question index, yes/no, pdf/index */
      stats->question_string_bytes +
      stats->tree_pattern_string_bytes +
      model->ntree * sizeof(uint32_t) +
      stats->pdf_count * sizeof(uint32_t) +
      stats->pdf_float_bytes;
}

static int model_has_data(const HTS_Model *model)
{
   return model != NULL && !(model->ntree == 0 && model->vector_length == 0 && model->tree == NULL);
}

static size_t count_loaded_models(const HTS_ModelSet *ms)
{
   size_t count = 0;
   size_t i;
   size_t j;

   if (ms->duration != NULL) {
      for (i = 0; i < ms->num_voices; i++)
         if (model_has_data(&ms->duration[i]))
            count++;
   }

   if (ms->stream != NULL) {
      for (i = 0; i < ms->num_voices; i++)
         for (j = 0; j < ms->num_streams; j++)
            if (model_has_data(&ms->stream[i][j]))
               count++;
   }

   if (ms->gv != NULL) {
      for (i = 0; i < ms->num_voices; i++)
         for (j = 0; j < ms->num_streams; j++)
            if (model_has_data(&ms->gv[i][j]))
               count++;
   }

   return count;
}

static size_t count_loaded_windows(const HTS_ModelSet *ms)
{
   size_t count = 0;
   size_t i;

   if (ms->window == NULL)
      return 0;

   for (i = 0; i < ms->num_streams; i++)
      count += ms->window[i].size;

   return count;
}

static int find_question_index(const HTS_Model *model, const HTS_Question *target, uint32_t question_offset, uint32_t *question_index)
{
   const HTS_Question *question;
   uint32_t index = question_offset;

   for (question = model->question; question != NULL; question = question->next) {
      if (question == target) {
         *question_index = index;
         return 1;
      }
      index++;
   }

   return 0;
}

static int append_patterns_raw(HtsRawBuffer *pattern_records, HtsRawBuffer *strings, const HTS_Pattern *pattern)
{
   for (; pattern != NULL; pattern = pattern->next) {
      HtsRawPatternRecord record;

      memset(&record, 0, sizeof(record));
      if (!append_string(strings, pattern->string, &record.string_offset) ||
          !raw_buffer_append(pattern_records, &record, sizeof(record)))
         return 0;
   }

   return 1;
}

static int append_questions_raw(HtsRawBuffer *question_records, HtsRawBuffer *qpattern_records, HtsRawBuffer *strings,
                                const HTS_Question *question)
{
   for (; question != NULL; question = question->next) {
      HtsRawQuestionRecord record;
      size_t pattern_count = count_patterns(question->head);

      memset(&record, 0, sizeof(record));
      if (!append_string(strings, question->string, &record.name_offset) ||
          !checked_u32(qpattern_records->size / sizeof(HtsRawPatternRecord), &record.pattern_offset) ||
          !checked_u32(pattern_count, &record.pattern_count) ||
          !append_patterns_raw(qpattern_records, strings, question->head) ||
          !raw_buffer_append(question_records, &record, sizeof(record)))
         return 0;
   }

   return 1;
}

static int append_node_raw(HtsRawBuffer *node_records, const HTS_Model *model, const HTS_Node *node,
                           uint32_t question_offset, uint32_t *node_index)
{
   HtsRawNodeRecord record;
   uint32_t current_index;

   if (node == NULL)
      return 0;

   if (!checked_u32(node_records->size / sizeof(HtsRawNodeRecord), &current_index))
      return 0;
   *node_index = current_index;

   memset(&record, 0, sizeof(record));
   record.source_index = (int32_t) node->index;
   if (!raw_buffer_append(node_records, &record, sizeof(record)))
      return 0;

   if (node->quest != NULL) {
      if (!find_question_index(model, node->quest, question_offset, &record.question_index))
         return 0;
      if (!append_node_raw(node_records, model, node->yes, question_offset, &record.yes_index) ||
          !append_node_raw(node_records, model, node->no, question_offset, &record.no_index))
         return 0;
   }
   if (!checked_u32(node->pdf, &record.pdf_index))
      return 0;

   memcpy(node_records->data + *node_index * sizeof(record), &record, sizeof(record));
   return 1;
}

static int append_tree_raw(HtsRawBuffer *tree_records, HtsRawBuffer *tpattern_records, HtsRawBuffer *node_records,
                           HtsRawBuffer *strings, const HTS_Model *model, const HTS_Tree *tree,
                           uint32_t question_offset)
{
   for (; tree != NULL; tree = tree->next) {
      HtsRawTreeRecord record;
      size_t pattern_count = count_patterns(tree->head);

      memset(&record, 0, sizeof(record));
      if (!checked_u32(tree->state, &record.state) ||
          !checked_u32(tpattern_records->size / sizeof(HtsRawPatternRecord), &record.pattern_offset) ||
          !checked_u32(pattern_count, &record.pattern_count) ||
          !append_patterns_raw(tpattern_records, strings, tree->head) ||
          !append_node_raw(node_records, model, tree->root, question_offset, &record.root_node_index) ||
          !raw_buffer_append(tree_records, &record, sizeof(record)))
         return 0;
   }

   return 1;
}

static int append_model_raw(HtsRawBuffer *model_records, HtsRawBuffer *question_records, HtsRawBuffer *qpattern_records,
                            HtsRawBuffer *tree_records, HtsRawBuffer *tpattern_records, HtsRawBuffer *node_records,
                            HtsRawBuffer *strings, HtsRawBuffer *npdf_values, HtsRawBuffer *pdf_values,
                            uint32_t kind, size_t voice_index, size_t stream_index, const HTS_Model *model)
{
   HtsRawModelRecord record;
   HtsModelInspectStats stats;
   size_t tree_state;
   size_t pdf_index;
   size_t values_per_pdf;
   size_t pdf_count = 0;
   uint32_t value32;

   if (!model_has_data(model))
      return 1;

   memset(&record, 0, sizeof(record));
   record.kind = kind;
   if (!checked_u32(voice_index, &record.voice_index) ||
       !checked_u32(stream_index, &record.stream_index) ||
       !checked_u32(model->vector_length, &record.vector_length) ||
       !checked_u32(model->num_windows, &record.num_windows) ||
       !checked_u32(model->ntree, &record.ntree) ||
       !checked_u32(npdf_values->size / sizeof(uint32_t), &record.npdf_offset) ||
       !checked_u32(pdf_values->size / sizeof(float), &record.pdf_float_offset) ||
       !checked_u32(question_records->size / sizeof(HtsRawQuestionRecord), &record.question_offset) ||
       !checked_u32(qpattern_records->size / sizeof(HtsRawPatternRecord), &record.qpattern_offset) ||
       !checked_u32(tree_records->size / sizeof(HtsRawTreeRecord), &record.tree_offset) ||
       !checked_u32(tpattern_records->size / sizeof(HtsRawPatternRecord), &record.tpattern_offset) ||
       !checked_u32(node_records->size / sizeof(HtsRawNodeRecord), &record.node_offset))
      return 0;

   record.is_msd = (uint32_t) model->is_msd;
   inspect_model_stats(model, &stats);
   if (!checked_u32(stats.question_count, &record.question_count) ||
       !checked_u32(stats.question_pattern_count, &record.qpattern_count) ||
       !checked_u32(stats.tree_count, &record.tree_count) ||
       !checked_u32(stats.tree_pattern_count, &record.tpattern_count) ||
       !checked_u32(stats.node_count, &record.node_count))
      return 0;

   if (!append_questions_raw(question_records, qpattern_records, strings, model->question) ||
       !append_tree_raw(tree_records, tpattern_records, node_records, strings, model, model->tree, record.question_offset))
      return 0;

   values_per_pdf = model->vector_length * model->num_windows * 2;
   if (model->is_msd)
      values_per_pdf++;

   for (tree_state = 2; tree_state <= model->ntree + 1; tree_state++) {
      if (!checked_u32(model->npdf[tree_state], &value32) || !append_u32(npdf_values, value32))
         return 0;
      pdf_count += model->npdf[tree_state];
   }

   for (tree_state = 2; tree_state <= model->ntree + 1; tree_state++) {
      for (pdf_index = 1; pdf_index <= model->npdf[tree_state]; pdf_index++) {
         if (!raw_buffer_append(pdf_values, model->pdf[tree_state][pdf_index], values_per_pdf * sizeof(float)))
            return 0;
      }
   }

   if (!checked_u32(model->ntree, &record.npdf_count) ||
       !checked_u32(pdf_count, &record.pdf_count) ||
       !checked_u32(pdf_count * values_per_pdf, &record.pdf_value_count))
      return 0;

   return raw_buffer_append(model_records, &record, sizeof(record));
}

static int append_window_raw(HtsRawBuffer *window_records, HtsRawBuffer *coefficients, size_t stream_index, const HTS_Window *window)
{
   size_t i;

   if (window == NULL)
      return 1;

   for (i = 0; i < window->size; i++) {
      HtsRawWindowRecord record;
      size_t count = (size_t) (window->r_width[i] - window->l_width[i] + 1);

      memset(&record, 0, sizeof(record));
      if (!checked_u32(stream_index, &record.stream_index) ||
          !checked_u32(i, &record.window_index) ||
          !checked_u32(coefficients->size / sizeof(float), &record.coefficient_offset) ||
          !checked_u32(count, &record.coefficient_count))
         return 0;
      record.left_width = (int32_t) window->l_width[i];
      record.right_width = (int32_t) window->r_width[i];

      if (!raw_buffer_append(window_records, &record, sizeof(record)) ||
          !raw_buffer_append(coefficients, &window->coefficient[i][window->l_width[i]], count * sizeof(float)))
         return 0;
   }

   return 1;
}

int hts_export_voice_to_raw(const char *voice_path, const char *output_path, size_t sample_rate, size_t frame_period, int show_memory_stats)
{
   HTS_Engine engine;
   HTS_ModelSet *ms;
   HtsRawVoiceHeader header;
   HtsRawBuffer model_records;
   HtsRawBuffer window_records;
   HtsRawBuffer question_records;
   HtsRawBuffer qpattern_records;
   HtsRawBuffer tree_records;
   HtsRawBuffer tpattern_records;
   HtsRawBuffer node_records;
   HtsRawBuffer strings;
   HtsRawBuffer npdf_values;
   HtsRawBuffer pdf_values;
   HtsRawBuffer window_coefficients;
   FILE *fp = NULL;
   char *voices[1];
   size_t i;
   size_t j;
   size_t output_sample_rate;
   size_t output_frame_period;
   int ok = 0;

   memset(&model_records, 0, sizeof(model_records));
   memset(&window_records, 0, sizeof(window_records));
   memset(&question_records, 0, sizeof(question_records));
   memset(&qpattern_records, 0, sizeof(qpattern_records));
   memset(&tree_records, 0, sizeof(tree_records));
   memset(&tpattern_records, 0, sizeof(tpattern_records));
   memset(&node_records, 0, sizeof(node_records));
   memset(&strings, 0, sizeof(strings));
   memset(&npdf_values, 0, sizeof(npdf_values));
   memset(&pdf_values, 0, sizeof(pdf_values));
   memset(&window_coefficients, 0, sizeof(window_coefficients));

   voices[0] = (char *) voice_path;
   wmem_init();
   if (show_memory_stats)
      print_memory_stats("after_wmem_init");

   HTS_Engine_initialize(&engine);
   if (HTS_Engine_load(&engine, voices, 1) != TRUE) {
      fprintf(stderr, "error: failed to load voice '%s'\n", voice_path);
      HTS_Engine_clear(&engine);
      goto cleanup;
   }

   if (show_memory_stats)
      print_memory_stats("after_voice_load");

   ms = &engine.ms;
   output_sample_rate = sample_rate != 0 ? sample_rate : ms->sampling_frequency;
   output_frame_period = frame_period != 0 ? frame_period : ms->frame_period;
   if (ms->duration != NULL) {
      for (i = 0; i < ms->num_voices; i++)
         if (!append_model_raw(&model_records, &question_records, &qpattern_records,
                               &tree_records, &tpattern_records, &node_records, &strings,
                               &npdf_values, &pdf_values, 0, i, 0, &ms->duration[i]))
            goto cleanup;
   }

   if (ms->stream != NULL) {
      for (i = 0; i < ms->num_voices; i++)
         for (j = 0; j < ms->num_streams; j++)
            if (!append_model_raw(&model_records, &question_records, &qpattern_records,
                                  &tree_records, &tpattern_records, &node_records, &strings,
                                  &npdf_values, &pdf_values, 1, i, j, &ms->stream[i][j]))
               goto cleanup;
   }

   if (ms->gv != NULL) {
      for (i = 0; i < ms->num_voices; i++)
         for (j = 0; j < ms->num_streams; j++)
            if (!append_model_raw(&model_records, &question_records, &qpattern_records,
                                  &tree_records, &tpattern_records, &node_records, &strings,
                                  &npdf_values, &pdf_values, 2, i, j, &ms->gv[i][j]))
               goto cleanup;
   }

   if (ms->window != NULL) {
      for (i = 0; i < ms->num_streams; i++)
         if (!append_window_raw(&window_records, &window_coefficients, i, &ms->window[i]))
            goto cleanup;
   }

   fp = fopen(output_path, "wb");
   if (fp == NULL) {
      fprintf(stderr, "error: failed to open output file '%s': errno=%d\n", output_path, errno);
      goto cleanup;
   }

   memset(&header, 0, sizeof(header));
   memcpy(header.magic, "HTSRAW2", 7);
   header.version = 2;
   header.section_count = 11;
   if (!checked_u32(output_sample_rate, &header.sampling_frequency) ||
       !checked_u32(output_frame_period, &header.frame_period) ||
       !checked_u32(ms->num_voices, &header.num_voices) ||
       !checked_u32(ms->num_states, &header.num_states) ||
       !checked_u32(ms->num_streams, &header.num_streams) ||
       !checked_u32(count_loaded_models(ms), &header.model_count) ||
       !checked_u32(count_loaded_windows(ms), &header.window_count))
      goto cleanup;

   if (!write_exact(fp, &header, sizeof(header)) ||
       !write_section(fp, "models", sizeof(HtsRawModelRecord), header.model_count, &model_records) ||
       !write_section(fp, "windows", sizeof(HtsRawWindowRecord), header.window_count, &window_records) ||
       !write_section(fp, "questions", sizeof(HtsRawQuestionRecord), (uint32_t) (question_records.size / sizeof(HtsRawQuestionRecord)), &question_records) ||
       !write_section(fp, "qpatterns", sizeof(HtsRawPatternRecord), (uint32_t) (qpattern_records.size / sizeof(HtsRawPatternRecord)), &qpattern_records) ||
       !write_section(fp, "trees", sizeof(HtsRawTreeRecord), (uint32_t) (tree_records.size / sizeof(HtsRawTreeRecord)), &tree_records) ||
       !write_section(fp, "tpatterns", sizeof(HtsRawPatternRecord), (uint32_t) (tpattern_records.size / sizeof(HtsRawPatternRecord)), &tpattern_records) ||
       !write_section(fp, "nodes", sizeof(HtsRawNodeRecord), (uint32_t) (node_records.size / sizeof(HtsRawNodeRecord)), &node_records) ||
       !write_section(fp, "strings", sizeof(char), (uint32_t) strings.size, &strings) ||
       !write_section(fp, "npdf_u32", sizeof(uint32_t), (uint32_t) (npdf_values.size / sizeof(uint32_t)), &npdf_values) ||
       !write_section(fp, "pdf_f32", sizeof(float), (uint32_t) (pdf_values.size / sizeof(float)), &pdf_values) ||
       !write_section(fp, "wincoef_f32", sizeof(float), (uint32_t) (window_coefficients.size / sizeof(float)), &window_coefficients))
      goto cleanup;

   fprintf(stdout,
           "export_voice: output=%s sample_rate=%u frame_period=%u source_sample_rate=%zu source_frame_period=%zu models=%u windows=%u questions=%zu qpatterns=%zu trees=%zu tpatterns=%zu nodes=%zu strings_bytes=%zu npdf_bytes=%zu pdf_float_bytes=%zu window_coefficient_bytes=%zu total_section_bytes=%zu\n",
           output_path,
           header.sampling_frequency,
           header.frame_period,
           ms->sampling_frequency,
           ms->frame_period,
           header.model_count,
           header.window_count,
           question_records.size / sizeof(HtsRawQuestionRecord),
           qpattern_records.size / sizeof(HtsRawPatternRecord),
           tree_records.size / sizeof(HtsRawTreeRecord),
           tpattern_records.size / sizeof(HtsRawPatternRecord),
           node_records.size / sizeof(HtsRawNodeRecord),
           strings.size,
           npdf_values.size,
           pdf_values.size,
           window_coefficients.size,
           model_records.size + window_records.size + question_records.size + qpattern_records.size +
           tree_records.size + tpattern_records.size + node_records.size + strings.size +
           npdf_values.size + pdf_values.size + window_coefficients.size);
   ok = 1;

cleanup:
   if (fp != NULL)
      fclose(fp);
   HTS_Engine_clear(&engine);
   if (show_memory_stats)
      print_memory_stats("after_engine_clear");
   raw_buffer_free(&model_records);
   raw_buffer_free(&window_records);
   raw_buffer_free(&question_records);
   raw_buffer_free(&qpattern_records);
   raw_buffer_free(&tree_records);
   raw_buffer_free(&tpattern_records);
   raw_buffer_free(&node_records);
   raw_buffer_free(&strings);
   raw_buffer_free(&npdf_values);
   raw_buffer_free(&pdf_values);
   raw_buffer_free(&window_coefficients);
   return ok;
}

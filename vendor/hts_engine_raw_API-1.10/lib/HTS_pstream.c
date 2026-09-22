/* ----------------------------------------------------------------- */
/*           The HMM-Based Speech Synthesis Engine "hts_engine API"  */
/*           developed by HTS Working Group                          */
/*           http://hts-engine.sourceforge.net/                      */
/* ----------------------------------------------------------------- */
/*                                                                   */
/*  Copyright (c) 2001-2015  Nagoya Institute of Technology          */
/*                           Department of Computer Science          */
/*                                                                   */
/*                2001-2008  Tokyo Institute of Technology           */
/*                           Interdisciplinary Graduate School of    */
/*                           Science and Engineering                 */
/*                                                                   */
/* All rights reserved.                                              */
/*                                                                   */
/* Redistribution and use in source and binary forms, with or        */
/* without modification, are permitted provided that the following   */
/* conditions are met:                                               */
/*                                                                   */
/* - Redistributions of source code must retain the above copyright  */
/*   notice, this list of conditions and the following disclaimer.   */
/* - Redistributions in binary form must reproduce the above         */
/*   copyright notice, this list of conditions and the following     */
/*   disclaimer in the documentation and/or other materials provided */
/*   with the distribution.                                          */
/* - Neither the name of the HTS working group nor the names of its  */
/*   contributors may be used to endorse or promote products derived */
/*   from this software without specific prior written permission.   */
/*                                                                   */
/* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND            */
/* CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,       */
/* INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF          */
/* MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE          */
/* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS */
/* BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,          */
/* EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED   */
/* TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,     */
/* DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON */
/* ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,   */
/* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY    */
/* OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE           */
/* POSSIBILITY OF SUCH DAMAGE.                                       */
/* ----------------------------------------------------------------- */

#ifndef HTS_PSTREAM_C
#define HTS_PSTREAM_C

#ifdef __cplusplus
#define HTS_PSTREAM_C_START extern "C" {
#define HTS_PSTREAM_C_END   }
#else
#define HTS_PSTREAM_C_START
#define HTS_PSTREAM_C_END
#endif                          /* __CPLUSPLUS */

HTS_PSTREAM_C_START;

#include <math.h>               /* for sqrt() */

/* hts_engine libraries */
#include "HTS_hidden.h"

/* HTS_finv: calculate 1.0/variance function */
static float HTS_finv(const float x)
{
   if (x >= INFTY2)
      return 0.0;
   if (x <= -INFTY2)
      return 0.0;
   if (x <= INVINF2 && x >= 0)
      return INFTY;
   if (x >= -INVINF2 && x < 0)
      return -INFTY;

   return (1.0f / x);
}

/* HTS_PStream_calc_wuw_and_wum: calcurate W'U^{-1}W and W'U^{-1}M */
static void HTS_PStream_calc_wuw_and_wum(HTS_PStream * pst, size_t m)
{
   size_t t, i, j;
   int shift;
   float wu;

   for (t = 0; t < pst->length; t++) {
      /* initialize */
      pst->sm[t].wum = 0.0;
      for (i = 0; i < pst->width; i++)
         pst->sm[t].wuw[i] = 0.0;

      /* calc WUW & WUM */
      for (i = 0; i < pst->win_size; i++)
         for (shift = pst->win_l_width[i]; shift <= pst->win_r_width[i]; shift++)
            if (((int) t + shift >= 0) && ((int) t + shift < pst->length) && (pst->win_coefficient[i][-shift] != 0.0)) {
               wu = pst->win_coefficient[i][-shift] * pst->sm[t + shift].ivar[i];
               pst->sm[t].wum += wu * pst->sm[t + shift].mean[i];
               for (j = 0; (j < pst->width) && (t + j < pst->length); j++)
                  if (((int) j <= pst->win_r_width[i] + shift) && (pst->win_coefficient[i][j - shift] != 0.0))
                     pst->sm[t].wuw[j] += wu * pst->win_coefficient[i][j - shift];
            }
   }
}


/* HTS_PStream_ldl_factorization: Factorize W'*U^{-1}*W to L*D*L' (L: lower triangular, D: diagonal) */
static void HTS_PStream_ldl_factorization(HTS_PStream * pst)
{
   size_t t, i, j;

   for (t = 0; t < pst->length; t++) {
      for (i = 1; (i < pst->width) && (t >= i); i++)
         pst->sm[t].wuw[0] -= pst->sm[t - i].wuw[i] * pst->sm[t - i].wuw[i] * pst->sm[t - i].wuw[0];

      for (i = 1; i < pst->width; i++) {
         for (j = 1; (i + j < pst->width) && (t >= j); j++)
            pst->sm[t].wuw[i] -= pst->sm[t - j].wuw[j] * pst->sm[t - j].wuw[i + j] * pst->sm[t - j].wuw[0];
         pst->sm[t].wuw[i] /= pst->sm[t].wuw[0];
      }
   }
}

/* HTS_PStream_forward_substitution: forward subtitution for mlpg */
static void HTS_PStream_forward_substitution(HTS_PStream * pst)
{
   size_t t, i;

   for (t = 0; t < pst->length; t++) {
      pst->sm[t].g = pst->sm[t].wum;
      for (i = 1; (i < pst->width) && (t >= i); i++)
         pst->sm[t].g -= pst->sm[t - i].wuw[i] * pst->sm[t - i].g;
   }
}

/* HTS_PStream_backward_substitution: backward subtitution for mlpg */
static void HTS_PStream_backward_substitution(HTS_PStream * pst, size_t m)
{
   size_t rev, t, i;

   for (rev = 0; rev < pst->length; rev++) {
      t = pst->length - 1 - rev;
      pst->par[t][m] = pst->sm[t].g / pst->sm[t].wuw[0];
      for (i = 1; (i < pst->width) && (t + i < pst->length); i++)
         pst->par[t][m] -= pst->sm[t].wuw[i] * pst->par[t + i][m];
   }
}

/* HTS_PStream_calc_gv: subfunction for mlpg using GV */
static void HTS_PStream_calc_gv(HTS_PStream * pst, size_t m, float *mean, float *vari)
{
   size_t t;

   *mean = 0.0;
   for (t = 0; t < pst->length; t++)
      if (pst->gv_switch[t])
         *mean += pst->par[t][m];
   *mean /= pst->gv_length;
   *vari = 0.0;
   for (t = 0; t < pst->length; t++)
      if (pst->gv_switch[t])
         *vari += (pst->par[t][m] - *mean) * (pst->par[t][m] - *mean);
   *vari /= pst->gv_length;
}

/* HTS_PStream_conv_gv: subfunction for mlpg using GV */
static void HTS_PStream_conv_gv(HTS_PStream * pst, size_t m)
{
   size_t t;
   float ratio;
   float mean;
   float vari;

   HTS_PStream_calc_gv(pst, m, &mean, &vari);
   ratio = sqrtf(pst->gv_mean[m] / vari);
   for (t = 0; t < pst->length; t++)
      if (pst->gv_switch[t])
         pst->par[t][m] = ratio * (pst->par[t][m] - mean) + mean;
}

/* HTS_PStream_calc_derivative: subfunction for mlpg using GV */
static float HTS_PStream_calc_derivative(HTS_PStream * pst, size_t m)
{
   size_t t, i;
   float mean;
   float vari;
   float dv;
   float h;
   float gvobj;
   float hmmobj;
   float w = 1.0f / (pst->win_size * pst->length);

   HTS_PStream_calc_gv(pst, m, &mean, &vari);
   gvobj = -0.5f * W2 * vari * pst->gv_vari[m] * (vari - 2.0f * pst->gv_mean[m]);
   dv = -2.0f * pst->gv_vari[m] * (vari - pst->gv_mean[m]) / pst->length;

   for (t = 0; t < pst->length; t++) {
      pst->sm[t].g = pst->sm[t].wuw[0] * pst->par[t][m];
      for (i = 1; i < pst->width; i++) {
         if (t + i < pst->length)
            pst->sm[t].g += pst->sm[t].wuw[i] * pst->par[t + i][m];
         if (t + 1 > i)
            pst->sm[t].g += pst->sm[t - i].wuw[i] * pst->par[t - i][m];
      }
   }

   for (t = 0, hmmobj = 0.0f; t < pst->length; t++) {
      hmmobj += W1 * w * pst->par[t][m] * (pst->sm[t].wum - 0.5f * pst->sm[t].g);
      h = -W1 * w * pst->sm[t].wuw[1 - 1] - W2 * 2.0f / (pst->length * pst->length) * ((pst->length - 1) * pst->gv_vari[m] * (vari - pst->gv_mean[m]) + 2.0f * pst->gv_vari[m] * (pst->par[t][m] - mean) * (pst->par[t][m] - mean));
      if (pst->gv_switch[t])
         pst->sm[t].g = 1.0f / h * (W1 * w * (-pst->sm[t].g + pst->sm[t].wum) + W2 * dv * (pst->par[t][m] - mean));
      else
         pst->sm[t].g = 1.0f / h * (W1 * w * (-pst->sm[t].g + pst->sm[t].wum));
   }

   return (-(hmmobj + gvobj));
}

/* HTS_PStream_gv_parmgen: function for mlpg using GV */
static void HTS_PStream_gv_parmgen(HTS_PStream * pst, size_t m)
{
   size_t t, i;
   float step = STEPINIT;
   float prev = 0.0;
   float obj;

   if (pst->gv_length == 0)
      return;

   HTS_PStream_conv_gv(pst, m);
   if (GV_MAX_ITERATION > 0) {
      HTS_PStream_calc_wuw_and_wum(pst, m);
      for (i = 1; i <= GV_MAX_ITERATION; i++) {
         obj = HTS_PStream_calc_derivative(pst, m);
         if (i > 1) {
            if (obj > prev)
               step *= STEPDEC;
            if (obj < prev)
               step *= STEPINC;
         }
         for (t = 0; t < pst->length; t++) {
            if (pst->gv_switch[t])
               pst->par[t][m] += step * pst->sm[t].g;
         }
         prev = obj;
      }
   }
}

static void HTS_PStream_set_current_feature(HTS_PStream * pst, HTS_SStreamSet * sss, size_t stream_index, size_t total_frame, size_t m)
{
   size_t state, frame, msd_frame, j, k;
   int shift;
   HTS_Boolean not_bound;

   if (pst->length == 0)
      return;

   if (HTS_SStreamSet_is_msd(sss, stream_index) == TRUE) {
      for (state = 0, frame = 0, msd_frame = 0; state < HTS_SStreamSet_get_total_state(sss); state++) {
         for (j = 0; j < HTS_SStreamSet_get_duration(sss, state); j++) {
            if (pst->msd_flag[frame] == TRUE) {
               for (k = 0; k < pst->win_size; k++) {
                  not_bound = TRUE;
                  for (shift = pst->win_l_width[k]; shift <= pst->win_r_width[k]; shift++) {
                     if ((int) frame + shift < 0 || (int) total_frame <= (int) frame + shift || pst->msd_flag[frame + shift] != TRUE) {
                        not_bound = FALSE;
                        break;
                     }
                  }
                  pst->sm[msd_frame].mean[k] = HTS_SStreamSet_get_mean(sss, stream_index, state, pst->vector_length * k + m);
                  if (not_bound || k == 0)
                     pst->sm[msd_frame].ivar[k] = HTS_finv(HTS_SStreamSet_get_vari(sss, stream_index, state, pst->vector_length * k + m));
                  else
                     pst->sm[msd_frame].ivar[k] = 0.0;
               }
               msd_frame++;
            }
            frame++;
         }
      }
   } else {
      for (state = 0, frame = 0; state < HTS_SStreamSet_get_total_state(sss); state++) {
         for (j = 0; j < HTS_SStreamSet_get_duration(sss, state); j++) {
            for (k = 0; k < pst->win_size; k++) {
               not_bound = TRUE;
               for (shift = pst->win_l_width[k]; shift <= pst->win_r_width[k]; shift++) {
                  if ((int) frame + shift < 0 || (int) total_frame <= (int) frame + shift) {
                     not_bound = FALSE;
                     break;
                  }
               }
               pst->sm[frame].mean[k] = HTS_SStreamSet_get_mean(sss, stream_index, state, pst->vector_length * k + m);
               if (not_bound || k == 0)
                  pst->sm[frame].ivar[k] = HTS_finv(HTS_SStreamSet_get_vari(sss, stream_index, state, pst->vector_length * k + m));
               else
                  pst->sm[frame].ivar[k] = 0.0;
            }
            frame++;
         }
      }
   }
}

/* HTS_PStream_mlpg: generate sequence of speech parameter vector maximizing its output probability for given pdf sequence */
static void HTS_PStream_mlpg(HTS_PStream * pst, HTS_SStreamSet * sss, size_t stream_index, size_t total_frame)
{
   size_t m;

   if (pst->length == 0)
      return;

   for (m = 0; m < pst->vector_length; m++) {
      HTS_PStream_set_current_feature(pst, sss, stream_index, total_frame, m);
      HTS_PStream_calc_wuw_and_wum(pst, m);
      HTS_PStream_ldl_factorization(pst);       /* LDL factorization */
      HTS_PStream_forward_substitution(pst);    /* forward substitution   */
      HTS_PStream_backward_substitution(pst, m);        /* backward substitution  */
      if (pst->gv_length > 0)
         HTS_PStream_gv_parmgen(pst, m);
   }
}

/* HTS_PStream_clear_work: free temporary matrices used only during MLPG */
static void HTS_PStream_clear_work(HTS_PStream * pst)
{
   if (pst->sm_mean_data) {
      HTS_free(pst->sm_mean_data);
      pst->sm_mean_data = NULL;
   }
   if (pst->sm_ivar_data) {
      HTS_free(pst->sm_ivar_data);
      pst->sm_ivar_data = NULL;
   }
   if (pst->sm_wuw_data) {
      HTS_free(pst->sm_wuw_data);
      pst->sm_wuw_data = NULL;
   }
   if (pst->sm) {
      HTS_free(pst->sm);
      pst->sm = NULL;
   }
}

/* HTS_SStreamSet_clear_stream_data: release one stream's SStream data after PStream generation */
static void HTS_SStreamSet_clear_stream_data(HTS_SStreamSet * sss, size_t stream_index)
{
   size_t j;
   HTS_SStream *sst;

   if (sss == NULL || sss->sstream == NULL || stream_index >= sss->nstream)
      return;

   sst = &sss->sstream[stream_index];
   if (sst->mean_data)
      HTS_free(sst->mean_data);
   if (sst->vari_data)
      HTS_free(sst->vari_data);
   if (sst->msd)
      HTS_free(sst->msd);
   if (sst->mean)
      HTS_free(sst->mean);
   if (sst->vari)
      HTS_free(sst->vari);
   if (sst->win_coefficient) {
      for (j = 0; j < sst->win_size; j++) {
         if (sst->win_coefficient[j]) {
            sst->win_coefficient[j] += sst->win_l_width[j];
            HTS_free(sst->win_coefficient[j]);
         }
      }
      HTS_free(sst->win_coefficient);
   }
   if (sst->win_l_width)
      HTS_free(sst->win_l_width);
   if (sst->win_r_width)
      HTS_free(sst->win_r_width);
   if (sst->gv_mean)
      HTS_free(sst->gv_mean);
   if (sst->gv_vari)
      HTS_free(sst->gv_vari);
   if (sst->gv_switch)
      HTS_free(sst->gv_switch);

   sst->vector_length = 0;
   sst->mean = NULL;
   sst->vari = NULL;
   sst->mean_data = NULL;
   sst->vari_data = NULL;
   sst->msd = NULL;
   sst->win_size = 0;
   sst->win_l_width = NULL;
   sst->win_r_width = NULL;
   sst->win_coefficient = NULL;
   sst->win_max_width = 0;
   sst->gv_mean = NULL;
   sst->gv_vari = NULL;
   sst->gv_switch = NULL;
}

/* HTS_PStreamSet_initialize: initialize parameter stream set */
void HTS_PStreamSet_initialize(HTS_PStreamSet * pss)
{
   pss->pstream = NULL;
   pss->nstream = 0;
   pss->total_frame = 0;
}

/* HTS_PStreamSet_create_internal: parameter generation using GV weight */
static HTS_Boolean HTS_PStreamSet_create_internal(HTS_PStreamSet *pss, HTS_SStreamSet *sss, float *msd_threshold, float *gv_weight, HTS_Boolean consume_sstream)
{
    size_t i, j;
    int shift;
    size_t frame, msd_frame, state;
    size_t feature_length;

    HTS_PStream *pst;

    if (pss->nstream != 0) {
        HTS_error(1, "HTS_PstreamSet_create: HTS_PStreamSet should be clear.\n");
        return FALSE;
    }

    /* initialize */
    pss->nstream = HTS_SStreamSet_get_nstream(sss);
    pss->pstream = (HTS_PStream *)HTS_calloc(pss->nstream, sizeof(HTS_PStream));
    pss->total_frame = HTS_SStreamSet_get_total_frame(sss);

    for (i = 0; i < pss->nstream; i++) {
        pst = &pss->pstream[i];
        if (HTS_SStreamSet_is_msd(sss, i) == TRUE) {      /* for MSD */
            /* create */
            pst->length = 0;
            pst->msd_flag = (HTS_Boolean *)HTS_calloc(pss->total_frame, sizeof(HTS_Boolean));
            for (state = 0, frame = 0; state < HTS_SStreamSet_get_total_state(sss); state++) {
                if (HTS_SStreamSet_get_msd(sss, i, state) > msd_threshold[i]) {
                    pst->length += HTS_SStreamSet_get_duration(sss, state);
                    for (j = 0; j < HTS_SStreamSet_get_duration(sss, state); j++) {
                        pst->msd_flag[frame] = TRUE;
                        frame++;
                    }
                }
                else {
                    for (j = 0; j < HTS_SStreamSet_get_duration(sss, state); j++) {
                        pst->msd_flag[frame] = FALSE;
                        frame++;
                    }
                }
            }
            pst->vector_length = HTS_SStreamSet_get_vector_length(sss, i);
            pst->width = HTS_SStreamSet_get_window_max_width(sss, i) * 2 + 1; /* band width of R */
            pst->win_size = HTS_SStreamSet_get_window_size(sss, i);
            if (pst->length > 0) {
                feature_length = pst->win_size;
                pst->sm = (HTS_SMatrices *)HTS_calloc(pst->length, sizeof(HTS_SMatrices));
                pst->sm_mean_data = (float *)HTS_calloc(pst->length * feature_length, sizeof(float));
                pst->sm_ivar_data = (float *)HTS_calloc(pst->length * feature_length, sizeof(float));
                pst->sm_wuw_data = (float *)HTS_calloc(pst->length * pst->width, sizeof(float));
                for (j = 0; j < pst->length; j++) {
                    HTS_SMatrices *sm = &pst->sm[j];
                    sm->mean = &pst->sm_mean_data[j * feature_length];
                    sm->ivar = &pst->sm_ivar_data[j * feature_length];
                    sm->wum = 0.0;
                    sm->wuw = &pst->sm_wuw_data[j * pst->width];
                    sm->g = 0.0;
                }
                pst->par = HTS_alloc_matrix(pst->length, pst->vector_length);
            }
            /* copy dynamic window */
            pst->win_l_width = (int *)HTS_calloc(pst->win_size, sizeof(int));
            pst->win_r_width = (int *)HTS_calloc(pst->win_size, sizeof(int));
            pst->win_coefficient = (float **)HTS_calloc(pst->win_size, sizeof(float*));
            for (j = 0; j < pst->win_size; j++) {
                pst->win_l_width[j] = HTS_SStreamSet_get_window_left_width(sss, i, j);
                pst->win_r_width[j] = HTS_SStreamSet_get_window_right_width(sss, i, j);
                if (pst->win_l_width[j] + pst->win_r_width[j] == 0)
                    pst->win_coefficient[j] = (float *)
                    HTS_calloc(-2 * pst->win_l_width[j] + 1, sizeof(float));
                else
                    pst->win_coefficient[j] = (float *)
                    HTS_calloc(-2 * pst->win_l_width[j], sizeof(float));
                pst->win_coefficient[j] -= pst->win_l_width[j];
                for (shift = pst->win_l_width[j]; shift <= pst->win_r_width[j]; shift++)
                    pst->win_coefficient[j][shift] = HTS_SStreamSet_get_window_coefficient(sss, i, j, shift);
            }
            /* copy GV */
            if (HTS_SStreamSet_use_gv(sss, i)) {
                pst->gv_mean = (float *)HTS_calloc(pst->vector_length, sizeof(float));
                pst->gv_vari = (float *)HTS_calloc(pst->vector_length, sizeof(float));
                for (j = 0; j < pst->vector_length; j++) {
                    pst->gv_mean[j] = HTS_SStreamSet_get_gv_mean(sss, i, j) * gv_weight[i];
                    pst->gv_vari[j] = HTS_SStreamSet_get_gv_vari(sss, i, j);
                }
                pst->gv_switch = (HTS_Boolean *)HTS_calloc(pst->length, sizeof(HTS_Boolean));
                for (j = 0, pst->gv_length = 0; j < pst->length; j++)
                    if (pst->gv_switch[j])
                        pst->gv_length++;
            }
            else {
                pst->gv_switch = NULL;
                pst->gv_length = 0;
                pst->gv_mean = NULL;
                pst->gv_vari = NULL;
            }
            /* copy pdfs */
            for (state = 0, frame = 0, msd_frame = 0; state < HTS_SStreamSet_get_total_state(sss); state++) {
                for (j = 0; j < HTS_SStreamSet_get_duration(sss, state); j++) {
                    if (pst->msd_flag[frame] == TRUE) {
                        if (pst->gv_switch != NULL)
                            pst->gv_switch[msd_frame] = HTS_SStreamSet_get_gv_switch(sss, i, state);
                        msd_frame++;
                    }
                    frame++;
                }
            }
        }
        else {                  /* for non MSD */
            /* create */
            pst->length = pss->total_frame;
            pst->msd_flag = NULL;
            pst->vector_length = HTS_SStreamSet_get_vector_length(sss, i);
            pst->width = HTS_SStreamSet_get_window_max_width(sss, i) * 2 + 1; /* band width of R */
            pst->win_size = HTS_SStreamSet_get_window_size(sss, i);
            if (pst->length > 0) {
                feature_length = pst->win_size;
                pst->sm = (HTS_SMatrices *)HTS_calloc(pst->length, sizeof(HTS_SMatrices));
                pst->sm_mean_data = (float *)HTS_calloc(pst->length * feature_length, sizeof(float));
                pst->sm_ivar_data = (float *)HTS_calloc(pst->length * feature_length, sizeof(float));
                pst->sm_wuw_data = (float *)HTS_calloc(pst->length * pst->width, sizeof(float));
                for (j = 0; j < pst->length; j++) {
                    HTS_SMatrices *sm = &pst->sm[j];
                    sm->mean = &pst->sm_mean_data[j * feature_length];
                    sm->ivar = &pst->sm_ivar_data[j * feature_length];
                    sm->wum = 0.0;
                    sm->wuw = &pst->sm_wuw_data[j * pst->width];
                    sm->g = 0.0;
                }
                pst->par = HTS_alloc_matrix(pst->length, pst->vector_length);
            }
            /* copy dynamic window */
            pst->win_l_width = (int *)HTS_calloc(pst->win_size, sizeof(int));
            pst->win_r_width = (int *)HTS_calloc(pst->win_size, sizeof(int));
            pst->win_coefficient = (float **)HTS_calloc(pst->win_size, sizeof(float*));
            for (j = 0; j < pst->win_size; j++) {
                pst->win_l_width[j] = HTS_SStreamSet_get_window_left_width(sss, i, j);
                pst->win_r_width[j] = HTS_SStreamSet_get_window_right_width(sss, i, j);
                if (pst->win_l_width[j] + pst->win_r_width[j] == 0)
                    pst->win_coefficient[j] = (float *)
                    HTS_calloc(-2 * pst->win_l_width[j] + 1, sizeof(float));
                else
                    pst->win_coefficient[j] = (float *)
                    HTS_calloc(-2 * pst->win_l_width[j], sizeof(float));
                pst->win_coefficient[j] -= pst->win_l_width[j];
                for (shift = pst->win_l_width[j]; shift <= pst->win_r_width[j]; shift++)
                    pst->win_coefficient[j][shift] = HTS_SStreamSet_get_window_coefficient(sss, i, j, shift);
            }
            /* copy GV */
            if (HTS_SStreamSet_use_gv(sss, i)) {
                pst->gv_mean = (float *)HTS_calloc(pst->vector_length, sizeof(float));
                pst->gv_vari = (float *)HTS_calloc(pst->vector_length, sizeof(float));
                for (j = 0; j < pst->vector_length; j++) {
                    pst->gv_mean[j] = HTS_SStreamSet_get_gv_mean(sss, i, j) * gv_weight[i];
                    pst->gv_vari[j] = HTS_SStreamSet_get_gv_vari(sss, i, j);
                }
                pst->gv_switch = (HTS_Boolean *)HTS_calloc(pst->length, sizeof(HTS_Boolean));
                for (j = 0, pst->gv_length = 0; j < pst->length; j++)
                    if (pst->gv_switch[j])
                        pst->gv_length++;
            }
            else {
                pst->gv_switch = NULL;
                pst->gv_length = 0;
                pst->gv_mean = NULL;
                pst->gv_vari = NULL;
            }
            /* copy pdfs */
            for (state = 0, frame = 0; state < HTS_SStreamSet_get_total_state(sss); state++) {
                for (j = 0; j < HTS_SStreamSet_get_duration(sss, state); j++) {
                    if (pst->gv_switch != NULL)
                        pst->gv_switch[frame] = HTS_SStreamSet_get_gv_switch(sss, i, state);
                    frame++;
                }
            }
        }
        /* parameter generation */
        HTS_PStream_mlpg(pst, sss, i, pss->total_frame);
        HTS_PStream_clear_work(pst);
        if (consume_sstream == TRUE)
            HTS_SStreamSet_clear_stream_data(sss, i);
    }

    return TRUE;
}

/* HTS_PStreamSet_create: parameter generation using GV weight */
HTS_Boolean HTS_PStreamSet_create(HTS_PStreamSet *pss, HTS_SStreamSet *sss, float *msd_threshold, float *gv_weight)
{
    return HTS_PStreamSet_create_internal(pss, sss, msd_threshold, gv_weight, FALSE);
}

/* HTS_PStreamSet_create_consuming_sstream: parameter generation and release per-stream SStream data after use */
HTS_Boolean HTS_PStreamSet_create_consuming_sstream(HTS_PStreamSet *pss, HTS_SStreamSet *sss, float *msd_threshold, float *gv_weight)
{
    return HTS_PStreamSet_create_internal(pss, sss, msd_threshold, gv_weight, TRUE);
}

/* HTS_PStreamSet_get_nstream: get number of stream */
size_t HTS_PStreamSet_get_nstream(HTS_PStreamSet * pss)
{
   return pss->nstream;
}

/* HTS_PStreamSet_get_vector_length: get feature length */
size_t HTS_PStreamSet_get_vector_length(HTS_PStreamSet * pss, size_t stream_index)
{
   return pss->pstream[stream_index].vector_length;
}

/* HTS_PStreamSet_get_total_frame: get total number of frame */
size_t HTS_PStreamSet_get_total_frame(HTS_PStreamSet * pss)
{
   return pss->total_frame;
}

/* HTS_PStreamSet_get_parameter: get parameter */
float HTS_PStreamSet_get_parameter(HTS_PStreamSet * pss, size_t stream_index, size_t frame_index, size_t vector_index)
{
   return pss->pstream[stream_index].par[frame_index][vector_index];
}

/* HTS_PStreamSet_get_parameter_vector: get parameter vector*/
float *HTS_PStreamSet_get_parameter_vector(HTS_PStreamSet * pss, size_t stream_index, size_t frame_index)
{
   return pss->pstream[stream_index].par[frame_index];
}

/* HTS_PStreamSet_get_msd_flag: get generated MSD flag per frame */
HTS_Boolean HTS_PStreamSet_get_msd_flag(HTS_PStreamSet * pss, size_t stream_index, size_t frame_index)
{
   return pss->pstream[stream_index].msd_flag[frame_index];
}

/* HTS_PStreamSet_is_msd: get MSD flag */
HTS_Boolean HTS_PStreamSet_is_msd(HTS_PStreamSet * pss, size_t stream_index)
{
   return pss->pstream[stream_index].msd_flag ? TRUE : FALSE;
}

/* HTS_PStreamSet_clear: free parameter stream set */
void HTS_PStreamSet_clear(HTS_PStreamSet * pss)
{
   size_t i, j;
   HTS_PStream *pstream;

   if (pss->pstream) {
      for (i = 0; i < pss->nstream; i++) {
         pstream = &pss->pstream[i];
         HTS_PStream_clear_work(pstream);
         if (pstream->par)
            HTS_free_matrix(pstream->par, pstream->length);
         if (pstream->msd_flag)
            HTS_free(pstream->msd_flag);
         if (pstream->win_coefficient) {
            for (j = 0; j < pstream->win_size; j++) {
               pstream->win_coefficient[j] += pstream->win_l_width[j];
               HTS_free(pstream->win_coefficient[j]);
            }
         }
         if (pstream->gv_mean)
            HTS_free(pstream->gv_mean);
         if (pstream->gv_vari)
            HTS_free(pstream->gv_vari);
         if (pstream->win_coefficient)
            HTS_free(pstream->win_coefficient);
         if (pstream->win_l_width)
            HTS_free(pstream->win_l_width);
         if (pstream->win_r_width)
            HTS_free(pstream->win_r_width);
         if (pstream->gv_switch)
            HTS_free(pstream->gv_switch);
      }
      HTS_free(pss->pstream);
   }
   HTS_PStreamSet_initialize(pss);
}

HTS_PSTREAM_C_END;

#endif                          /* !HTS_PSTREAM_C */

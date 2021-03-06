#include "license_pbs.h" /* See here for the software license */
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include "pbs_error.h"
#include "pbs_job.h"
#include "req_delete.h"
#include "test_req_delete.h"
#include "server.h"
#include "work_task.h"

#define PURGE_SUCCESS 1

int delete_inactive_job(job **, const char *);
void force_purge_work(job *pjob);
void *delete_all_work(void *vp);
void ensure_deleted(struct work_task *ptask);
int  apply_job_delete_nanny(job *pjob, int delay);
void job_delete_nanny(struct work_task *ptask);
void post_job_delete_nanny(batch_request *preq_sig);
int forced_jobpurge(job *pjob, batch_request *preq);
void post_delete_mom2(struct work_task *pwt);
int handle_delete_all(batch_request *preq, batch_request *preq_tmp, char *Msg);
int handle_single_delete(batch_request *preq, batch_request *preq_tmp, char *Msg);

extern long keep_seconds;
extern int bad_queue;
extern int bad_relay;
extern int signal_issued;
extern int nanny;
extern int br_freed;
extern int alloc_work;
extern struct server server;
extern const char *delpurgestr;

struct all_jobs  alljobs;

START_TEST(test_handle_single_delete)
  {
  batch_request *preq = (batch_request *)calloc(1,sizeof(batch_request));
  strcpy(preq->rq_ind.rq_delete.rq_objname, "2.napali");
  fail_unless(handle_single_delete(preq, preq, NULL) == PBSE_NONE);
  fail_unless(preq->rq_noreply == FALSE);

  strcpy(preq->rq_ind.rq_delete.rq_objname, "1.napali");
  fail_unless(handle_single_delete(preq, preq, NULL) == PBSE_NONE);
  fail_unless(preq->rq_noreply == TRUE);

  }
END_TEST

START_TEST(test_handle_delete_all)
  {
  batch_request preq;
  memset(&preq, 0, sizeof(preq));

  fail_unless(handle_delete_all(&preq, &preq, NULL) == PBSE_NONE);
  fail_unless(preq.rq_noreply == TRUE);
  }
END_TEST

START_TEST(test_duplicate_request)
  {
  batch_request *preq = (batch_request *)calloc(1, sizeof(batch_request));
  batch_request *dup;
  alloc_work = 0;
  fail_unless(duplicate_request(preq) == NULL);

  alloc_work = 1;
  preq->rq_perm = 1;
  strcpy(preq->rq_user, "dbeer");
  strcpy(preq->rq_host, "napali");
  preq->rq_extend = strdup("tom");
  preq->rq_type = PBS_BATCH_RunJob;
  preq->rq_ind.rq_run.rq_destin = strdup("napali");

  dup = duplicate_request(preq);
  fail_unless(dup != NULL);
  fail_unless(!strcmp(dup->rq_extend, "tom"));
  fail_unless(!strcmp(dup->rq_user, "dbeer"));
  fail_unless(!strcmp(dup->rq_host, "napali"));
  fail_unless(!strcmp(dup->rq_extend, "tom"));
  fail_unless(!strcmp(dup->rq_ind.rq_run.rq_destin, "napali")); 
  }
END_TEST 

START_TEST(test_post_delete_mom2)
  {
  struct work_task *ptask;

  ptask = (struct work_task *)calloc(1, sizeof(struct work_task));
  ptask->wt_mutex = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t));
  signal_issued = FALSE;
  post_delete_mom2(ptask);
  fail_unless(signal_issued == FALSE);

  signal_issued = FALSE;
  ptask = (struct work_task *)calloc(1, sizeof(struct work_task));
  ptask->wt_mutex = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t));
  ptask->wt_parm1 = strdup("1.napali");
  post_delete_mom2(ptask);
  fail_unless(signal_issued == TRUE);
  }
END_TEST

START_TEST(test_forced_jobpurge)
  {
  job           *pjob;
  batch_request *preq;

  pjob = (job *)calloc(1, sizeof(job));
  preq = (batch_request *)calloc(1, sizeof(batch_request));

  preq->rq_extend = strdup(delpurgestr);
  nanny = 0;
  fail_unless(forced_jobpurge(pjob, preq) == -1);

  preq->rq_perm = ATR_DFLAG_MGRD;
  fail_unless(forced_jobpurge(pjob, preq) == PURGE_SUCCESS);
 
  preq->rq_extend = NULL;
  fail_unless(forced_jobpurge(pjob, preq) == PBSE_NONE);

  nanny = 1;
  }
END_TEST

START_TEST(test_delete_all_work)
  {
  //struct all_jobs  alljobs;
  job             *pjob;
  batch_request   *preq;

  pjob = (job *)calloc(1, sizeof(job));
  pjob->ji_mutex = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t));
  pthread_mutex_init(pjob->ji_mutex,NULL);

  preq = (batch_request *)calloc(1, sizeof(batch_request));
  preq->rq_extend = strdup(delpurgestr);
  nanny = 0;

  initialize_all_jobs_array(&alljobs);
  insert_job(&alljobs, pjob);

  /* no lock should remain on job after delete_all_work() */
  /*  so test by making sure we can set lock */
  fail_unless(delete_all_work((void *) preq) == NULL && pthread_mutex_trylock(pjob->ji_mutex) == 0);

  nanny = 1;
  }
END_TEST

START_TEST(test_post_job_delete_nanny)
  {
  batch_request *preq_sig;
  
  br_freed = FALSE;
  post_job_delete_nanny(NULL);
  fail_unless(br_freed == FALSE);

  preq_sig = (batch_request *)calloc(1, sizeof(batch_request));
  br_freed = FALSE;
  nanny = 0;
  post_job_delete_nanny(preq_sig);
  fail_unless(br_freed == TRUE);

  preq_sig = (batch_request *)calloc(1, sizeof(batch_request));
  br_freed = FALSE;
  nanny = 1;
  strcpy(preq_sig->rq_ind.rq_signal.rq_jid, "2.napali");
  post_job_delete_nanny(preq_sig);
  fail_unless(br_freed == TRUE);

  preq_sig = (batch_request *)calloc(1, sizeof(batch_request));
  br_freed = FALSE;
  nanny = 1;
  strcpy(preq_sig->rq_ind.rq_signal.rq_jid, "1.napali");
  post_job_delete_nanny(preq_sig);
  fail_unless(br_freed == TRUE);

  preq_sig = (batch_request *)calloc(1, sizeof(batch_request));
  br_freed = FALSE;
  nanny = 1;
  strcpy(preq_sig->rq_ind.rq_signal.rq_jid, "1.napali");
  preq_sig->rq_reply.brp_code = PBSE_UNKJOBID;
  post_job_delete_nanny(preq_sig);
  fail_unless(br_freed == TRUE);
  }
END_TEST

START_TEST(test_job_delete_nanny)
  {
  struct work_task *ptask;
  ptask = (struct work_task *)calloc(1, sizeof(struct work_task));
  ptask->wt_mutex = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t));
  ptask->wt_parm1 = strdup("1.napali");

  nanny = 0;
  job_delete_nanny(ptask);
  fail_unless(signal_issued == FALSE);

  ptask = (struct work_task *)calloc(1, sizeof(struct work_task));
  ptask->wt_mutex = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t));
  ptask->wt_parm1 = strdup("1.napali");
  nanny = 1;
  job_delete_nanny(ptask);
  fail_unless(signal_issued == TRUE);

  }
END_TEST

START_TEST(test_apply_job_delete_nanny)
  {
  job *pjob = (job *)calloc(1, sizeof(job));

  fail_unless(apply_job_delete_nanny(pjob, -1) == -1);
  fail_unless(pjob->ji_has_delete_nanny == FALSE);
  fail_unless(apply_job_delete_nanny(pjob, 0) == PBSE_NONE);
  fail_unless(pjob->ji_has_delete_nanny == TRUE);
  pjob->ji_has_delete_nanny = FALSE;
  fail_unless(apply_job_delete_nanny(pjob, 10) == PBSE_NONE);
  fail_unless(pjob->ji_has_delete_nanny == TRUE);

  fail_unless(apply_job_delete_nanny(pjob, 1) == PBSE_NONE);
  }
END_TEST

START_TEST(test_ensure_deleted)
  {
  struct work_task *ptask = (struct work_task *)calloc(1, sizeof(struct work_task));
  ptask->wt_mutex = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t));
  ptask->wt_parm1 = strdup("1.napali");

  ensure_deleted(ptask);
  }
END_TEST

START_TEST(test_delete_inactive_job)
  {
  job *pjob = (job *)calloc(1, sizeof(pjob));

  fail_unless(delete_inactive_job((job **)NULL, NULL) == PBSE_BAD_PARAMETER);
  
  keep_seconds = 10;
  fail_unless(delete_inactive_job(&pjob, NULL) == PBSE_NONE);
  fail_unless(pjob->ji_qs.ji_state == JOB_STATE_COMPLETE);

  pjob = (job *)calloc(1, sizeof(pjob));
  pjob->ji_qs.ji_state = JOB_STATE_QUEUED;
  bad_queue = 1;
  fail_unless(delete_inactive_job(&pjob, NULL) == PBSE_NONE);
  fail_unless(pjob->ji_qs.ji_state == JOB_STATE_COMPLETE);
  bad_queue = 0;

  pjob = (job *)calloc(1, sizeof(pjob));
  pjob->ji_qs.ji_svrflags |= JOB_SVFLG_CHECKPOINT_FILE;
  fail_unless(delete_inactive_job(&pjob, NULL) == PBSE_NONE);
  fail_unless(pjob->ji_qs.ji_state == JOB_STATE_EXITING);
  fail_unless(pjob->ji_momhandle = -1);

  pjob = (job *)calloc(1, sizeof(pjob));
  pjob->ji_qs.ji_svrflags = JOB_SVFLG_StagedIn;
  fail_unless(delete_inactive_job(&pjob, NULL) == PBSE_NONE);
  fail_unless(pjob == NULL);

  pjob = (job *)calloc(1, sizeof(pjob));
  bad_relay = 1;
  pjob->ji_qs.ji_svrflags = JOB_SVFLG_StagedIn;
  fail_unless(delete_inactive_job(&pjob, NULL) == PBSE_NONE);
  fail_unless(pjob == NULL);
  bad_relay = 0;
  }
END_TEST

START_TEST(test_force_purge_work)
  {
  job *pjob = (job *)calloc(1, sizeof(job));

  pjob->ji_wattr[JOB_ATR_exec_host].at_val.at_str = strdup("bob");
  force_purge_work(pjob);
  /* normally pjob wouldn't be valid at this point, but I've made the functions 
   * that free set these values in the scaffolding so we can test what happened */
  fail_unless(pjob->ji_wattr[JOB_ATR_exec_host].at_val.at_str == NULL);
  fail_unless(pjob->ji_qs.ji_state == JOB_STATE_COMPLETE);
  }
END_TEST

Suite *req_delete_suite(void)
  {
  Suite *s = suite_create("req_delete_suite methods");
  TCase *tc_core = tcase_create("tests");
  tcase_add_test(tc_core, test_delete_inactive_job);
  tcase_add_test(tc_core, test_force_purge_work);
  tcase_add_test(tc_core, test_delete_all_work);
  tcase_add_test(tc_core, test_ensure_deleted);
  tcase_add_test(tc_core, test_apply_job_delete_nanny);
  tcase_add_test(tc_core, test_job_delete_nanny);
  tcase_add_test(tc_core, test_post_job_delete_nanny);
  tcase_add_test(tc_core, test_forced_jobpurge);
  tcase_add_test(tc_core, test_post_delete_mom2);
  suite_add_tcase(s, tc_core);
  
  tc_core = tcase_create("more");
  tcase_add_test(tc_core, test_duplicate_request);
  tcase_add_test(tc_core, test_handle_delete_all);
  tcase_add_test(tc_core, test_handle_single_delete);
  suite_add_tcase(s, tc_core);

  return s;
  }

void rundebug()
  {
  }

int main(void)
  {
  int number_failed = 0;
  SRunner *sr = NULL;
  rundebug();
  server.sv_attr_mutex = (pthread_mutex_t *)calloc(1, sizeof(pthread_mutex_t));
  sr = srunner_create(req_delete_suite());
  srunner_set_log(sr, "req_delete_suite.log");
  srunner_run_all(sr, CK_NORMAL);
  number_failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return number_failed;
  }

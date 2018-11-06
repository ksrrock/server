/* Copyright (c) 2018, MariaDB Corporation
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */


static const char *stage_names[]=
{"START", "FLUSH", "WAIT_FOR_FLUSH", "LOCK_COMMIT", "END", 0};

TYPELIB backup_stage_names=
{ array_elements(stage_names)-1, "", stage_names, 0 };


bool backup_start(THD *thd);
bool backup_flush(THD *thd);
bool backup_wait_for_flush(THD *thd);
bool backup_lock_commit(THD *thd);
bool backup_end(THD *thd);

/**
  Run next stage of backup
*/

bool backup_stage(THD *thd, enum backup_stage stage)
{
  uint stage;

  if (stage == BACKUP_START)
  {
    if (thd->backup_stage == BACKUP_FINISHED)
      thd->backup_stage= stage;
    else
    {
      my_error(ER_WRONG_BACKUP_STAGE, MYF(0), stage_names[stage], "BACKUP NOT STARTED");
      return 1;
    }
  }
  else if ((uint) thd->backup_stage >= (uint) stage)
  {
    my_error(ER_WRONG_BACKUP_STAGE, MYF(0), stage_names[stage],
             stage_names[thd->backup_stage]);
    return 1;
  }

  for (; 
       (uint) thd->backup_stage <= (uint) stage ;
       thd->backup_stage= (uint) thd->backup_stage+1)
  {
    bool res;
    switch (thd->backup_stage) {
    case BACKUP_STAGE_START:
      if (!(res= backup_start(thd)))
        break;
      thd->backup_stage= BACKUP_FINISHED;
      break;
    case BACKUP_FLUSH:
      res= backup_flush(thd);
      break;
    case BACKUP_WAIT_FOR_FLUSH:
      res= backup_wait_for_flush(thd);
      break;
    case BACKUP_LOCK_COMMIT:
      res= backup_lock_commit(thd);
      break;
    case BACKUP_END:
      res= backup_end(thd);
      break;
    case BACKUP_FLUSH:
      DBUG_ASSERT(0);
      res= 0;
    }
    if (res)
    {
      my_error(ER_BACKUP_STAGE_FAILED, MYF(0), stage_names[(uint) stage]);
      return 1;
    }
  }
  return 0;
}


/**
  Start the backup

  - Wait for previous backup to stop running
  - Start service to log changed tables (TODO)
  - Block purge of redo files (Required at least for Aria)
*/

bool backup_start(THD *thd)
{
  PSI_stage_info saved_stage= {0, "", 0};
 
  mysql_mutex_lock(&LOCK_backup);
  thd->enter_cond(&COND_backup, &LOCK_backup, stage_waiting_for_backup,
                  &saved_stage);
  while (backup_running && !thd->killed)
    mysql_cond_wait(start_cond, cond_lock);

  if (thd->killed)
  {
    mysql_cond_signal(&COND_BACKUP);
    thd->EXIT_COND(&saved_stage);
    return 1;
  }
  backup_running= 1;
  thd->EXIT_COND(&saved_stage);

  ha_prepare_for_backup();
  return(0);
}
  

bool backup_flush(THD *thd);
bool backup_wait_for_flush(THD *thd);
bool backup_lock_commit(THD *thd);

bool backup_end(THD *thd)
{
  ha_end_backup();
  mysql_mutex_lock(&LOCK_backup);
  backup_running= 0;
  mysql_cond_signal(&COND_BACKUP);
  mysql_mutex_unlock(&LOCK_backup);
  return 0;
}

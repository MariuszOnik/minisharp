/* MiniSharp - what the menu (main_3ds.c) offers to the editor. */
#ifndef MS_APP_H
#define MS_APP_H

#define APP_DIR "sdmc:/minisharp"

/* runs "file.cs" or a project "Name/" from APP_DIR and shows its errors */
void run_file(const char* name);
/* after run_file: "" or the error, e.g. "Ship.cs line 12: ..." / "line 12: ..." / "line 3 (Game.Update): ..." */
extern char app_run_error[320];

#endif

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#include <gtk/gtk.h>
#include <libappindicator/app-indicator.h>

struct cpu_stat {
	long	user;
	long	nice;
	long	system;
	long	idle;
	long	iowait;
	long	irq;
	long	softirq;
	long	steal;
	long	guest;
	long	guest_nice;
	long	reserved[8];
	long	sum;
};

static struct cpu_stat	cpu[2];
static struct cpu_stat	*prev = cpu;
static struct cpu_stat	*now = cpu+1;

struct mem_stat {
	long	total;
	long	free;
	long	available;
};

static int cpu_fd, mem_fd;

static float get_cpu_usage(void)
{
	struct cpu_stat *tmp;
	double total, idle;
	char buffer[1024];
	char *bp;
	char *token;
	long *np;
	int rc;
	int i;

	if(cpu_fd <= 0) return -1;
	
	tmp = prev;
	prev = now;
	now = tmp;
	np = (long *)now;

	lseek(cpu_fd, 0, SEEK_SET);
	rc = read(cpu_fd, buffer, 1023);
	if(rc < 0) {
		printf("stat read fail, %s\n", strerror(errno));
		return -1;
	}

	buffer[rc] = 0;

	for(bp = buffer; *bp && *bp != '\n'; bp++)
		;

	if(*bp != '\n') return -1;
	*bp = 0;

//	printf("%s\n", buffer);

	token = strtok_r(buffer, " ", &bp);

	i = 0;
	now->sum = 0;
	while((token = strtok_r(NULL, " ", &bp))) {
		np[i] = atol(token);
		now->sum += np[i];
		i++;
	}

	total = now->sum - prev->sum;
	idle = now->idle - prev->idle;
	return (total - idle) / total;
}

static float get_mem_usage(void)
{
	struct mem_stat st;
	char buffer[256];
	char *bp;
	char *token;
	int rc;
	char key[32];
	double used;

	if(mem_fd <= 0) return -1;
	
	lseek(mem_fd, 0, SEEK_SET);
	rc = read(mem_fd, buffer, 128);
	if(rc < 0) {
		printf("meminfo read fail, %s\n", strerror(errno));
		return -1;
	}

	buffer[rc] = 0;

//	printf("%s\n", buffer);

	token = strtok_r(buffer, "\n", &bp);
	sscanf(token, "%s%ld kB", key, &st.total);

	token = strtok_r(NULL, "\n", &bp);
	sscanf(token, "%s%ld kB", key, &st.free);

	token = strtok_r(NULL, "\n", &bp);
	sscanf(token, "%s%ld kB", key, &st.available);

	used = st.total - st.available;

	return used / st.total;
}

struct proc_entry {
	char *pid;
	char *cmd;
	char *mem;
};

static GList *get_mem_top_process(int n)
{
	FILE *pipe;
	char line[1024];
	char *cmd;
	char *bp;
	GList *glist = NULL;

	if(asprintf(&cmd, "ps aux | sort -rn -k +4") < 0) {
		printf("%s: asprintf failed, %s\n", __func__, strerror(errno));
		return NULL;
	}

	pipe = popen(cmd, "r");
	free(cmd);
	if(!pipe) {
		printf("%s: popen failed, %s\n", __func__, strerror(errno));
		return NULL;
	}

	while(n > 0 && fgets(line, 1024, pipe)) {
		struct proc_entry *proc;
		char *pid;
		char *mem;
		char *cmd;
		char *s;

		for(s = line; *s; s++)
			if(*s == '\n') {
				*s = 0;
				break;
			}

		pid	= strtok_r(line, " ", &bp);
		pid	= strtok_r(NULL, " ", &bp);
		mem	= strtok_r(NULL, " ", &bp);
		mem	= strtok_r(NULL, " ", &bp);
		cmd	= strtok_r(NULL, " ", &bp);
		cmd	= strtok_r(NULL, " ", &bp);
		cmd	= strtok_r(NULL, " ", &bp);
		cmd	= strtok_r(NULL, " ", &bp);
		cmd	= strtok_r(NULL, " ", &bp);
		cmd	= strtok_r(NULL, " ", &bp);
		cmd	= strtok_r(NULL, " ", &bp);

		for(s = cmd + strlen(cmd) - 1; s != cmd; s--)
			if(*s == '/') {
				cmd = s + 1;
				break;
			}

		proc = malloc(sizeof(*proc));
		if(!proc) {
			printf("%s, malloc failed\n", __func__);
			break;
		}
		proc->pid = strdup(pid);
		proc->cmd = strdup(cmd);
		proc->mem = strdup(mem);

		glist = g_list_append(glist, proc);
		n--;
	}

	while(fgets(line, 1024, pipe))
		;

	pclose(pipe);
	return glist;
}

static void menu_item_activate(GtkMenuItem *menuitem, gpointer user_data)
{
	long pid = (long)(g_object_get_data(G_OBJECT(menuitem), "pid"));
	char command[32];

	sprintf(command, "kill %ld", pid);
	printf("%s: command '%s', pid = %ld\n", __func__, command, pid);

	system(command);
}

static void mem_list_cb(gpointer data, gpointer user_data)
{
	struct proc_entry *proc = data;
	GtkMenuShell *menu_shell = user_data;
	GtkWidget *item;
	char buffer[128];

	sprintf(buffer, "%s  -  %s (%s)", proc->mem, proc->cmd, proc->pid);
	item = gtk_menu_item_new_with_label(buffer);
	g_object_set_data(G_OBJECT(item), "pid", (gpointer)atol(proc->pid));
	g_signal_connect(GTK_MENU_ITEM(item), "activate", G_CALLBACK(menu_item_activate), menu_shell);
	gtk_menu_shell_append(menu_shell, item);
}

static void proc_destroy(gpointer data)
{
	struct proc_entry *proc = data;

	if(!proc) return ;

	free(proc->pid);
	free(proc->mem);
	free(proc->cmd);
}

static gboolean update(AppIndicator *indicator)
{
	float cpu_load = get_cpu_usage();
	float mem_load = get_mem_usage();
	char buffer[32];
	GtkMenu *menu;
	GList *glist;

	sprintf(buffer, "%02.0fC %02.0fM", cpu_load * 100, mem_load * 100);

	app_indicator_set_label(indicator, buffer, buffer);

	menu = app_indicator_get_menu(indicator);
	if(menu)
		gtk_widget_destroy(GTK_WIDGET(menu));

	menu = GTK_MENU(gtk_menu_new());

	glist = get_mem_top_process(5);
	g_list_foreach(glist, mem_list_cb, menu);
	g_list_free_full(glist, proc_destroy);

	app_indicator_set_menu(indicator, menu);
	gtk_widget_show_all(GTK_WIDGET(menu));

	return TRUE;
}

int main (int argc, char **argv)
{
	AppIndicator *indicator;

	gtk_init (&argc, &argv);

	/* Indicator */
	indicator = app_indicator_new ("indicator-tinymonitor", "device",
			APP_INDICATOR_CATEGORY_HARDWARE);
	app_indicator_set_icon_theme_path(indicator, "/home/new/github/indicator-tinymonitor");
	app_indicator_set_icon_full(indicator, "device", "device");

	app_indicator_set_status(indicator, APP_INDICATOR_STATUS_ACTIVE);

	app_indicator_set_label(indicator, "tinymonitor", "tinymonitor");

	GtkMenu *menu = GTK_MENU(gtk_menu_new());
	GtkWidget *item = gtk_menu_item_new_with_label("Exit");
	gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

	app_indicator_set_menu(indicator, menu);

	gtk_widget_show_all(GTK_WIDGET(menu));

	cpu_fd = open("/proc/stat", O_RDONLY);
	if(cpu_fd < 0)
		printf("stat open fail, %s\n", strerror(errno));

	mem_fd = open("/proc/meminfo", O_RDONLY);
	if(mem_fd < 0)
		printf("meminfo open fail, %s\n", strerror(errno));

	update(indicator);

	g_timeout_add(5000, (GSourceFunc)update, indicator);

	gtk_main();

	return 0;
}


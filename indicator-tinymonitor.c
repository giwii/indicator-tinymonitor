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

static gboolean update(AppIndicator *indicator)
{
	float cpu_load = get_cpu_usage();
	float mem_load = get_mem_usage();
	char buffer[32];

	sprintf(buffer, "%02.0fC %02.0fM", cpu_load * 100, mem_load * 100);

	app_indicator_set_label(indicator, buffer, buffer);

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


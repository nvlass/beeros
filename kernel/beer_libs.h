#ifndef BEER_LIBS_H
#define BEER_LIBS_H

/* Compile and run the embedded pure-beerlang libraries (virtio drivers,
 * ext2, beer.fs, beer.input), in dependency order. Needs beer.mem and
 * beer.blk loaded first. */
void beer_load_libraries(void);

/* Run /etc/init.beer (beer.fs/run-init!); last thing before the REPL */
void beer_run_init(void);

#endif

#!/usr/bin/env Rscript
library(rmarkdown)
library(xfun)

machine_names <- c("kone", "sole", "riscv")

for (machine_name in machine_names) {
    xfun::Rscript_call(
        rmarkdown::render,
        list(input = "wasmbounds.Rmd",
            output_format = "pdf_document",
            output_file = paste0("wasmbounds-", machine_name, ".pdf"),
            params = list(machine_name = machine_name)
        )
    )
}

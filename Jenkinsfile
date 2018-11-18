#!/usr/bin/env groovy

// This pipeline is designed to run on Esri-internal CI infrastructure.

import groovy.transform.Field


// -- PIPELINE LIBRARIES

@Library('psl@simo6772/prt-apps-library')
import com.esri.zrh.jenkins.PipelineSupportLibrary 
import com.esri.zrh.jenkins.JenkinsTools
import com.esri.zrh.jenkins.ce.CityEnginePipelineLibrary
import com.esri.zrh.jenkins.ce.PrtAppPipelineLibrary

@Field def psl = new PipelineSupportLibrary(this)
@Field def cepl = new CityEnginePipelineLibrary(this, psl)
@Field def papl = new PrtAppPipelineLibrary(cepl)


// -- GLOBAL DEFINITIONS

@Field final String REPO   = 'git@github.com:Esri/palladio.git'
@Field final String SOURCE = "palladio.git/src"


// -- SETUP

properties([ disableConcurrentBuilds() ])
psl.runsHere('testing', 'development')


// -- PIPELINE

stage('build') {
	stageImpl()
}

def stageImpl() {
	Map tasks = [:]
	tasks << taskGenPalladio()
	cepl.runParallel(tasks)
}


// -- TASK CONFIGURATIONS

// TOOD: add cesdk and houdini versions to configuration list
List getConfigs(String prefix) {
	return [
		[ name: "${prefix}-lin", ba: psl.BA_RHEL7, os: 'rhel7', tc: 'gcc48', arch: 'x86_64', bt: 'Release' ],
		// [ name: "${prefix}-win", ba: psl.BA_WIN10, os: 'win10', tc: 'vc141', arch: 'x86_64', bt: 'Release' ]
	]
}


// -- TASK GENERATORS

Map taskGenPalladio() {
	final String taskPrefix = "${env.STAGE_NAME}-palladio"
	return cepl.generateTasks(getConfigs(taskPrefix), this.&taskBuildPalladio, taskPrefix)
}


// -- TASK BUILDERS

def taskBuildPalladio(cfg) {
	List defs = [
		[ key: 'BUILD_NUMBER_OR_SO', val: env.BUILD_NUMBER ]
	]
	papl.buildConfig(REPO, SOURCE, 'package', cfg, [], defs)
}
